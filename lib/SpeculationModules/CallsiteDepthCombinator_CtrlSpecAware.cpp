#define DEBUG_TYPE "callsite-depth-combinator-aa"

#include "llvm/ADT/Statistic.h"
#include "llvm/Support/Debug.h"

#include "scaf/MemoryAnalysisModules/AnalysisTimeout.h"
#include "scaf/MemoryAnalysisModules/Introspection.h"
#include "scaf/SpeculationModules/ControlSpecRemed.h"
#include "scaf/SpeculationModules/CallsiteDepthCombinator_CtrlSpecAware.h"
#include "scaf/SpeculationModules/KillFlow_CtrlSpecAware.h"
#include "scaf/Utilities/CallSiteFactory.h"

#include <ctime>

namespace liberty
{
  using namespace llvm;
  using namespace arcana::noelle;

  STATISTIC(numHits,     "Num cache hits");
  STATISTIC(numEligible, "Num eligible");
  STATISTIC(numFlowTests,"Num flow tests");

  STATISTIC(numKillScalarStoreAfterSrc,   "Num flows killed: store scalar after src");
  STATISTIC(numKillScalarStoreBeforeDst,  "Num flows killed: store scalar before dst");
  STATISTIC(numKillScalarStoreBetween,    "Num flows killed: store scalar between src and dst");
  STATISTIC(numKillScalarStoreInLoadCtx,  "Num flows killed: store scalar within dst context");

  STATISTIC(numKillScalarLoadAfterSrc,    "Num flows killed: load scalar after src");
  STATISTIC(numKillScalarLoadBeforeDst,   "Num flows killed: load scalar before dst");
  STATISTIC(numKillScalarLoadBetween,     "Num flows killed: load scalar between src and dst");
  STATISTIC(numKillScalarLoadInStoreCtx,  "Num flows killed: load scalar within src context");

  STATISTIC(numKillAggregateLoad,         "Num flows killed: load from killed aggregate");
  STATISTIC(numKillAggregateStore,        "Num flows killed: store to killed aggregate");

  bool CallsiteDepthCombinator_CtrlSpecAware::runOnModule(Module &mod)
  {
    const DataLayout &DL = mod.getDataLayout();
    InitializeLoopAA(this, DL);

    killflow = getAnalysisIfAvailable< KillFlow_CtrlSpecAware >();
    if( !killflow )
    {
      errs() << "KillFlow_CtrlSpecAware not available, creating a private instance.\n";
      killflow = new KillFlow_CtrlSpecAware();
      killflow->setEffectiveNextAA( getNextAA() );
      killflow->setEffectiveTopAA( getTopAA() );
      killflow->setModuleLoops( & getAnalysis< ModuleLoops >() );
      killflow->setDL(&DL);
      //killflow->setProxy(this);
    }


    return false;
  }

  bool CallsiteDepthCombinator_CtrlSpecAware::isEligible(const Instruction *i) const
  {
    CallSite cs = getCallSite(i);
    if( !cs.getInstruction() )
      return false;

    const Function *f = cs.getCalledFunction();
    if( !f )
      return false;

    if( f->isDeclaration() )
      return false;

    return true;
  }

  void CallsiteDepthCombinator_CtrlSpecAware::getAnalysisUsage(AnalysisUsage &AU) const
  {
    LoopAA::getAnalysisUsage(AU);
    AU.addRequired< ModuleLoops >();
    //AU.addRequired< DominatorTreeWrapperPass >();
    //AU.addRequired< PostDominatorTreeWrapperPass >();
    //AU.addRequired< LoopInfoWrapperPass >();
//    AU.addRequired< KillFlow_CtrlSpecAware >();
    AU.setPreservesAll();        // Does not transform code
  }

  static const Instruction *getToplevelInst(const CtxInst_CtrlSpecAware &ci)
  {
    const Instruction *inst = ci.getInst();
    for(const CallsiteContext_CtrlSpecAware *ctx = ci.getContext().front(); ctx; ctx=ctx->getParent() )
      inst = ctx->getLocationWithinParent();

    return inst;
  }

  /// Determine if it is possible for a store
  /// 'src' to flow to a load 'dst' across
  /// the backedge of L.
  bool CallsiteDepthCombinator_CtrlSpecAware::mayFlowCrossIter(
      const CtxInst_CtrlSpecAware &write, const CtxInst_CtrlSpecAware &read,
      const Loop *L, KillFlow_CtrlSpecAware &kill, Remedies &R,
      time_t queryStart, unsigned Timeout) {
    const Instruction *src = getToplevelInst(write),
                      *dst = getToplevelInst(read);

    return mayFlowCrossIter(kill,src,dst,L,write,read,R,queryStart,Timeout);
  }

  bool CallsiteDepthCombinator_CtrlSpecAware::mayFlowCrossIter(
    KillFlow_CtrlSpecAware &kill,
    const Instruction *src,
    const Instruction *dst,
    const Loop *L,
    const CtxInst_CtrlSpecAware &write,
    const CtxInst_CtrlSpecAware &read,
    Remedies &R,
    time_t queryStart,unsigned Timeout)
  {
    ++numFlowTests;

    LoopAA *top = kill.getTopAA();
    INTROSPECT(errs() << "Test flow from " << write << " to " << read << " {\n");
//      enterIntrospectionRegion(false);
    Remedies tmpR;
    ModRefResult q = top->modref(write.getInst(), Before, read.getInst(), L, tmpR);
//      exitIntrospectionRegion();
    INTROSPECT(errs() << "} Exit test flow +--> " << q << '\n');
    if( q == NoModRef || q == Ref ) {
      LoopAA::appendRemedies(R, tmpR);
      return false;
    }

    std::shared_ptr<ControlSpecRemedy> remedy =
        std::shared_ptr<ControlSpecRemedy>(new ControlSpecRemedy());
    remedy->cost = DEFAULT_CTRL_REMED_COST;
    remedy->brI = nullptr;

    // May have been a flow.
    // Try to prove that the flow was killed.
    INTROSPECT(errs() << "CallsiteDepthIterator: Maybe flow\n"
                      << "  from: " << write << '\n'
                      << "    to: " << read << '\n');

    INTROSPECT(errs() << "- s0\n");

    // Was a store killed between the two operations?
    if( const StoreInst *store = dyn_cast< StoreInst >(write.getInst()) )
    {
      const Value *ptr = store->getPointerOperand();
      if( kill.pointerKilledAfter(L, ptr, src, true, queryStart, Timeout) )
      {
        ++numKillScalarStoreAfterSrc;
        R.insert(remedy);
        return false;
      }

      INTROSPECT(errs() << "- s0.1\n");

      if( kill.pointerKilledBefore(L, ptr, dst, true, queryStart, Timeout) )
      {
        ++numKillScalarStoreBeforeDst;
        R.insert(remedy);
        return false;
      }

      INTROSPECT(errs() << "- s0.2\n");

      if( read.getContext().kills(kill, ptr, read.getInst(), true, false, queryStart, Timeout) )
      {
        ++numKillScalarStoreInLoadCtx;
        R.insert(remedy);
        return false;
      }
    }

    INTROSPECT(errs() << "- s1\n");

    // Was a load killed between the two operations?
    if( const LoadInst *load = dyn_cast< LoadInst >(read.getInst()) )
    {
      const Value *ptr = load->getPointerOperand();
      if( kill.pointerKilledBefore(L, ptr, dst, true, queryStart, Timeout) )
      {
        ++numKillScalarLoadBeforeDst;
        R.insert(remedy);
        return false;
      }

      INTROSPECT(errs() << "- s1.1\n");

      if( kill.pointerKilledAfter(L, ptr, src, true, queryStart, Timeout) )
      {
        ++numKillScalarLoadAfterSrc;
        R.insert(remedy);
        return false;
      }

      INTROSPECT(errs() << "- s1.2\n");

      if( write.getContext().kills(kill, ptr, write.getInst(), false, false, queryStart, Timeout) )
      {
        ++numKillScalarLoadInStoreCtx;
        R.insert(remedy);
        return false;
      }
    }

    INTROSPECT(errs() << "- s2\n");

    // Were the store's underlying objects killed?
    if( const StoreInst *store = dyn_cast< StoreInst >(write.getInst()) )
    {
      const Value *ptr = store->getPointerOperand();
      bool allObjectsKilled = true;
      UO objects;
      write.getContext().getUnderlyingObjects(kill,ptr,write.getInst(),objects,false);
      for(UO::iterator i=objects.begin(), e=objects.end(); i!=e; ++i)
      {
        const Value *object = *i;

        if( kill.aggregateKilledAfter(L, object, src, queryStart, Timeout) )
          continue;
        if( kill.aggregateKilledBefore(L, object, dst, queryStart, Timeout) )
          continue;

        allObjectsKilled = false;
        break;
      }

      if( allObjectsKilled )
      {
        ++numKillAggregateStore;
        R.insert(remedy);
        return false;
      }
    }

    INTROSPECT(errs() << "- s3\n");

    // Were the load's underlying objects killed?
    if( const LoadInst *load = dyn_cast< LoadInst >(read.getInst()) )
    {
      const Value *ptr = load->getPointerOperand();
      bool allObjectsKilled = true;
      UO objects;
      read.getContext().getUnderlyingObjects(kill,ptr,read.getInst(),objects,true);
      for(UO::iterator i=objects.begin(), e=objects.end(); i!=e; ++i)
      {
        const Value *object = *i;

        if( kill.aggregateKilledAfter(L, object, src, queryStart, Timeout) )
          continue;
        if( kill.aggregateKilledBefore(L, object, dst, queryStart, Timeout) )
          continue;

        allObjectsKilled = false;
        break;
      }

      if( allObjectsKilled )
      {
        ++numKillAggregateLoad;
        R.insert(remedy);
        return false;
      }
    }

    INTROSPECT(errs() << "- s4\n");

    return true;
  }

  bool CallsiteDepthCombinator_CtrlSpecAware::mayFlowIntraIter(
    KillFlow_CtrlSpecAware &kill,
    const Instruction *src,
    const Instruction *dst,
    const Loop *L,
    const CtxInst_CtrlSpecAware &write,
    const CtxInst_CtrlSpecAware &read,
    Remedies &R)
  {
    ++numFlowTests;

    LoopAA *top = kill.getTopAA();
    INTROSPECT(errs() << "Test flow from " << write << " to " << read << " {\n");
//      enterIntrospectionRegion(false);
    Remedies tmpR;
    ModRefResult q = top->modref(write.getInst(), Same, read.getInst(), L, tmpR);
//      exitIntrospectionRegion();
    INTROSPECT(errs() << "} Exit test flow +--> " << q << '\n');
    if( q == NoModRef || q == Ref ) {
      LoopAA::appendRemedies(R, tmpR);
      return false;
    }

    std::shared_ptr<ControlSpecRemedy> remedy =
        std::shared_ptr<ControlSpecRemedy>(new ControlSpecRemedy());
    remedy->cost = DEFAULT_CTRL_REMED_COST;
    remedy->brI = nullptr;

    // May have been a flow.
    // Try to prove that the flow was killed.
    INTROSPECT(errs() << "CallsiteDepthIterator: Maybe flow\n"
                      << "  from: " << write << '\n'
                      << "    to: " << read << '\n');

    INTROSPECT(errs() << "- s0\n");

    // Was a store killed between the two operations?
    if( const StoreInst *store = dyn_cast< StoreInst >(write.getInst()) )
    {
      const Value *ptr = store->getPointerOperand();
      if( kill.pointerKilledBetween(L, ptr, src, dst) )
      {
        ++numKillScalarStoreBetween;
        R.insert(remedy);
        return false;
      }

      INTROSPECT(errs() << "- s0.1\n");

      if( read.getContext().kills(kill, ptr, read.getInst(), true) )
      {
        ++numKillScalarStoreInLoadCtx;
        R.insert(remedy);
        return false;
      }
    }

    INTROSPECT(errs() << "- s1\n");

    // Was a load killed between the two operations?
    if( const LoadInst *load = dyn_cast< LoadInst >(read.getInst()) )
    {
      const Value *ptr = load->getPointerOperand();
      if( kill.pointerKilledBetween(L, ptr, src,dst) )
      {
        ++numKillScalarLoadBetween;
        R.insert(remedy);
        return false;
      }

      INTROSPECT(errs() << "- s1.1\n");

      if( write.getContext().kills(kill, ptr, write.getInst(), false) )
      {
        ++numKillScalarLoadInStoreCtx;
        R.insert(remedy);
        return false;
      }
    }

    INTROSPECT(errs() << "- s2\n");

    // Were the store's underlying objects killed?
    if( const StoreInst *store = dyn_cast< StoreInst >(write.getInst()) )
    {
      const Value *ptr = store->getPointerOperand();
      bool allObjectsKilled = true;
      UO objects;
      write.getContext().getUnderlyingObjects(kill,ptr,write.getInst(),objects,false);
      for(UO::iterator i=objects.begin(), e=objects.end(); i!=e; ++i)
      {
        const Value *object = *i;

        if( kill.aggregateKilledBetween(L, object, src, dst) )
          continue;

        allObjectsKilled = false;
        break;
      }

      if( allObjectsKilled )
      {
        ++numKillAggregateStore;
        R.insert(remedy);
        return false;
      }
    }

    INTROSPECT(errs() << "- s3\n");

    // Were the load's underlying objects killed?
    if( const LoadInst *load = dyn_cast< LoadInst >(read.getInst()) )
    {
      const Value *ptr = load->getPointerOperand();
      bool allObjectsKilled = true;
      UO objects;
      read.getContext().getUnderlyingObjects(kill,ptr,read.getInst(),objects,true);
      for(UO::iterator i=objects.begin(), e=objects.end(); i!=e; ++i)
      {
        const Value *object = *i;

        if( kill.aggregateKilledBetween(L, object, src, dst) )
          continue;

        allObjectsKilled = false;
        break;
      }

      if( allObjectsKilled )
      {
        ++numKillAggregateLoad;
        R.insert(remedy);
        return false;
      }
    }

    INTROSPECT(errs() << "- s4\n");

    return true;
  }

  bool CallsiteDepthCombinator_CtrlSpecAware::doFlowSearchCrossIter(
      const Instruction *src, const Instruction *dst, const Loop *L,
      KillFlow_CtrlSpecAware &kill, Remedies &R, CIPairs *allFlowsOut,
      time_t queryStart, unsigned Timeout) {

    ReverseStoreSearch_CtrlSpecAware writes(src,kill,queryStart,Timeout);
    INTROSPECT(
      errs() << "LiveOuts {\n";
      // List all live-outs and live-ins.
      // This is really inefficient; a normal
      // query enumerates only as many as are necessary
      // before it witnesses a flow.
      for(InstSearch_CtrlSpecAware::iterator i=writes.begin(), e=writes.end(); i!=e; ++i)
      {
        const CtxInst_CtrlSpecAware &write = *i;
        errs() << "LiveOut(" << *src << ") write: " << write << '\n';
      }
      errs() << "}\n";
    );

    return doFlowSearchCrossIter(src,dst,L, writes,kill,R,allFlowsOut,queryStart, Timeout);
  }

  bool CallsiteDepthCombinator_CtrlSpecAware::doFlowSearchCrossIter(
      const Instruction *src, const Instruction *dst, const Loop *L,
      InstSearch_CtrlSpecAware &writes, KillFlow_CtrlSpecAware &kill,
      Remedies &R, CIPairs *allFlowsOut, time_t queryStart, unsigned Timeout) {
    ForwardLoadSearch_CtrlSpecAware reads(dst,kill,queryStart,Timeout);
    INTROSPECT(
      errs() << "LiveIns {\n";

      for(InstSearch_CtrlSpecAware::iterator j=reads.begin(), f=reads.end(); j!=f; ++j)
      {
        const CtxInst_CtrlSpecAware &read = *j;
        errs() << "LiveIn(" << *dst << ") read: " << read << '\n';
      }

      errs() << "}\n";
    );

    return doFlowSearchCrossIter(src,dst,L, writes,reads, kill,R,allFlowsOut,queryStart, Timeout);
  }

  bool CallsiteDepthCombinator_CtrlSpecAware::doFlowSearchCrossIter(
      const Instruction *src, const Instruction *dst, const Loop *L,
      InstSearch_CtrlSpecAware &writes, InstSearch_CtrlSpecAware &reads,
      KillFlow_CtrlSpecAware &kill, Remedies &R, CIPairs *allFlowsOut,
      time_t queryStart, unsigned Timeout) {
    const bool stopAfterFirst = (allFlowsOut == 0);
    bool isFlow = false;

    // Not yet in cache.  Look it up.
    for(InstSearch_CtrlSpecAware::iterator i=writes.begin(), e=writes.end(); i!=e; ++i)
    {
      const CtxInst_CtrlSpecAware &write = *i;
//        errs() << "Write: " << write << '\n';

      for(InstSearch_CtrlSpecAware::iterator j=reads.begin(), f=reads.end(); j!=f; ++j)
      {
        const CtxInst_CtrlSpecAware &read = *j;
//          errs() << "  Read: " << read << '\n';

        if(Timeout > 0 && queryStart > 0)
        {
          time_t now;
          time(&now);
          if( (now - queryStart) > Timeout )
          {
            errs() << "CDC::doFlowSearchCrossIter Timeout\n";
            return true;
          }
        }

        if( !mayFlowCrossIter(kill, src,dst,L, write,read, R, queryStart, Timeout) )
          continue;

        // TODO
        // Is there some way that the ReverseStoreSearch_CtrlSpecAware
        // and ForwardLoadSearch_CtrlSpecAware can (i) first, return
        // unexpanded callsites, and (ii) allow this loop
        // to ask them to expand those callsites, if
        // necessary, but (iii) Leave those callsites
        // unexpanded for later iterations of the loop?

        INTROSPECT(
          errs() << "Can't disprove flow\n"
                 << "\tfrom: " << write << '\n'
                 << "\t  to: " << read  << '\n');
        LLVM_DEBUG(
          errs() << "Can't disprove flow\n"
                 << "\tfrom: " << write << '\n'
                 << "\t  to: " << read  << '\n');

        if( allFlowsOut )
          allFlowsOut->push_back( CIPair(write,read) );
        isFlow = true;

        if( stopAfterFirst && isFlow )
          break;
      }

      if( stopAfterFirst && isFlow )
        break;
    }

    return isFlow;
  }

  LoopAA::ModRefResult CallsiteDepthCombinator_CtrlSpecAware::modref(
    const Instruction *inst1,
    TemporalRelation Rel,
    const Instruction *inst2,
    const Loop *L,
    Remedies &R)
  {
    //ModRefResult result = LoopAA::modref(inst1,Rel,inst2,L,R);
    //if( result == NoModRef || result == Ref )
    //  return result;
    if( Rel == Same )
      return LoopAA::modref(inst1,Rel,inst2,L, R);
    if( !L->contains(inst1) || !L->contains(inst2) )
      return LoopAA::modref(inst1,Rel,inst2,L, R);
    if( !isEligible(inst1) && !isEligible(inst2) )
      return LoopAA::modref(inst1,Rel,inst2,L, R);

    /*
    if( !inst1->mayReadFromMemory() )
      result = ModRefResult(result & ~Ref);
    if( !inst1->mayWriteToMemory() )
      return ModRefResult(result & ~Mod);
    */

    const Instruction *src=inst1, *dst=inst2;
    if( Rel == After )
      std::swap(src,dst);

    if( !src->mayWriteToMemory()
    ||  !dst->mayReadFromMemory() )
      return LoopAA::modref(inst1,Rel,inst2,L, R);

    // Maybe turn-on introspection
    bool introspect = false;
    if( WatchCallsitePair )
    {
      CallSite cs1 = getCallSite(inst1),
               cs2 = getCallSite(inst2);
      if( cs1.getInstruction() && cs2.getInstruction() )
        if( const Function *f1 = cs1.getCalledFunction() )
          if( const Function *f2 = cs2.getCalledFunction() )
            if( f1->getName() == FirstCallee )
              if( f2->getName() == SecondCallee )
                introspect = true;
    }

    else if( WatchCallsite2Store )
    {
      CallSite cs1 = getCallSite(inst1);
      const StoreInst *st2 = dyn_cast< StoreInst >(inst2);

      if( cs1.getInstruction() && st2 )
        if( const Function *f1 = cs1.getCalledFunction() )
          if( f1->getName() == FirstCallee )
            if( st2->getPointerOperand()->getName() == StorePtrName )
              introspect = true;
    }

    else if( WatchStore2Callsite )
    {
      const StoreInst *st1 = dyn_cast< StoreInst >(inst1);
      CallSite cs2 = getCallSite(inst2);

      if( cs2.getInstruction() && st1 )
        if( const Function *f2 = cs2.getCalledFunction() )
          if( f2->getName() == SecondCallee )
            if( st1->getPointerOperand()->getName() == StorePtrName )
              introspect = true;
    }

    if( introspect )
      enterIntrospectionRegion();

    INTROSPECT(ENTER(inst1,Rel,inst2,L));
    //INTROSPECT(errs() << "Starting with " << result << '\n');

    ++numEligible;

    // This analysis is trying to find
    // a flow of values through memory.
    bool isFlow = false;
    Remedies isFlowTmpR;

    // Cached result?
    IIKey key(src,Before,dst,L);
    if( iiCache.count(key) )
    {
      // Use result from cache.
      ++numHits;
      isFlow = iiCache[key];
      for (auto remed : iiCacheR[key])
        isFlowTmpR.insert(remed);
    }

    else
    {
      time_t queryStart=0;
      if( AnalysisTimeout > 0 )
        time(&queryStart);
      isFlow = iiCache[key] = doFlowSearchCrossIter(
          src, dst, L, *killflow, isFlowTmpR, 0, queryStart, AnalysisTimeout);
      iiCacheR[key] = isFlowTmpR;
      queryStart = 0;
    }

    ModRefResult flowResult = ModRef;

    // Interpret the isFlow result w.r.t. LoopAA Before/After query semantics.
    if( !isFlow )
    {
      LLVM_DEBUG(errs() << "No flow from " << *src << " to " << *dst << '\n');

      if (Rel == Before)
        flowResult = Ref;
      else if (Rel == After)
        flowResult = Mod;
    }

    INTROSPECT(EXIT(inst1,Rel,inst2,L,flowResult));
    if( introspect )
      exitIntrospectionRegion();

    return LoopAA::chain(R, inst1, Rel, inst2, L, flowResult, isFlowTmpR);
  }

  LoopAA::ModRefResult CallsiteDepthCombinator_CtrlSpecAware::modref(
    const Instruction *i1,
    TemporalRelation Rel,
    const Value *p2,
    unsigned s2,
    const Loop *L,
    Remedies &R)
  {
    ModRefResult result = LoopAA::modref(i1,Rel,p2,s2,L,R);
    if( result == NoModRef || result == Ref )
      return result;
    if( Rel == Same )
      return result;
    if( !L->contains(i1) )
      return result;
    if( !isEligible(i1) )
      return result;
    INTROSPECT( ENTER(i1,Rel,p2,s2,L) );

    // TODO

    INTROSPECT( EXIT(i1,Rel,p2,s2,L,result) );
    return result;
  }

  char CallsiteDepthCombinator_CtrlSpecAware::ID = 0;

  static RegisterPass<CallsiteDepthCombinator_CtrlSpecAware>
  XX("callsite-depth-combinator-ctrl-spec-aa", "Alias analysis with deep inspection of callsites with ctrl spec awareness", false, true);
  static RegisterAnalysisGroup<liberty::LoopAA> Y(XX);


}
