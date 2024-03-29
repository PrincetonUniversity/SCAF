#define DEBUG_TYPE "classify"

#include "llvm/IR/IntrinsicInst.h"
#include "llvm/ADT/Statistic.h"

#include "scaf/MemoryAnalysisModules/CallsiteDepthCombinator.h"
#include "scaf/MemoryAnalysisModules/CallsiteSearch.h"
#include "scaf/MemoryAnalysisModules/KillFlow.h"
#include "scaf/MemoryAnalysisModules/PureFunAA.h"
#include "scaf/MemoryAnalysisModules/SemiLocalFunAA.h"
#include "scaf/MemoryAnalysisModules/SimpleAA.h"
//#include "scaf/SpeculationModules/LAMPLoadProfile.h"
//#include "scaf/SpeculationModules/LampOracleAA.h"
#include "scaf/SpeculationModules/LoopProf/Targets.h"
//#include "scaf/SpeculationModules/CommutativeLibsAA.h"
#include "scaf/SpeculationModules/EdgeCountOracleAA.h"
#include "scaf/SpeculationModules/PointsToAA.h"
#include "scaf/SpeculationModules/PtrResidueAA.h"
#include "scaf/SpeculationModules/ReadOnlyAA.h"
#include "scaf/SpeculationModules/ShortLivedAA.h"
#include "scaf/SpeculationModules/SmtxAA.h"
//#include "scaf/SpeculationModules/TXIOAA.h"
#include "scaf/SpeculationModules/CallsiteDepthCombinator_CtrlSpecAware.h"
#include "scaf/SpeculationModules/Classify.h"
#include "scaf/SpeculationModules/ControlSpeculator.h"
#include "scaf/SpeculationModules/KillFlow_CtrlSpecAware.h"
#include "scaf/SpeculationModules/PredictionSpeculator.h"
#include "scaf/SpeculationModules/Read.h"
#include "scaf/SpeculationModules/ProfilePerformanceEstimator.h"
#include "scaf/Utilities/CallSiteFactory.h"
#include "scaf/Utilities/GepRange.h"
#include "scaf/Utilities/GetMemOper.h"
#include "scaf/Utilities/GlobalMalloc.h"
#include "scaf/Utilities/ModuleLoops.h"
#include "scaf/Utilities/StableHash.h"
#include "scaf/Utilities/Timer.h"
#include "scaf/Utilities/ReportDump.h"

#include <set>

namespace liberty
{
template <>
stable_hash_code stable_hash<SpecPriv::Ctx const&>(SpecPriv::Ctx const &ctx)
{
  return stable_combine((int)ctx.type, ctx.fcn, ctx.header, ctx.parent);
}

template <>
stable_hash_code stable_hash<SpecPriv::AU const&>(SpecPriv::AU const &au)
{
  return stable_combine((int)au.type, au.value, au.ctx);
}

namespace SpecPriv
{
using namespace llvm;
using namespace arcana::noelle;

STATISTIC(numClassified, "Parallel regions selected #regression");

static cl::opt<bool> PrintFootprints(
  "print-loop-footprints", cl::init(false), cl::NotHidden,
  cl::desc("Print memory footprint of each hot loop"));
static cl::opt<unsigned> NumSubHeaps(
  "num-subheaps", cl::init(8), cl::NotHidden,
  cl::desc("Sub-divide heaps into N subheaps"));


void Classify::getAnalysisUsage(AnalysisUsage &au) const
{
  au.addRequired< TargetLibraryInfoWrapperPass >();
  au.addRequired< ModuleLoops >();
  //au.addRequired< LAMPLoadProfile >();
  au.addRequired< ReadPass >();
  au.addRequired< PtrResidueSpeculationManager >();
  au.addRequired< SmtxSpeculationManager>();
  au.addRequired< ProfileGuidedControlSpeculator >();
  au.addRequired< ProfileGuidedPredictionSpeculator >();
  au.addRequired< LoopAA >();
  au.addRequired< KillFlow >();
  au.addRequired< PureFunAA >();
  au.addRequired< SemiLocalFunAA >();
  au.addRequired<KillFlow_CtrlSpecAware>();
  au.addRequired<CallsiteDepthCombinator_CtrlSpecAware>();
  au.addRequired< Targets >();
  au.addRequired< ProfilePerformanceEstimator >();
  au.setPreservesAll();
}


bool Classify::runOnModule(Module &mod)
{
  REPORT_DUMP(errs() << "#################################################\n"
               << " Classification\n\n\n");
  ModuleLoops &mloops = getAnalysis< ModuleLoops >();
  Targets &targets = getAnalysis< Targets >();

  // We augment the LoopAA stack with all
  // of the speculative AAs...

  {
    PerformanceEstimator *perf = &getAnalysis< ProfilePerformanceEstimator >();

    // CtrlSpec
    ControlSpeculation *ctrlspec = getAnalysis< ProfileGuidedControlSpeculator >().getControlSpecPtr();
    EdgeCountOracle edgeaa(ctrlspec);
    edgeaa.InitializeLoopAA(this, mod.getDataLayout());

    // LampOracle does not produce remedies. Need to use SmtxAA instead
    // LAMP
    //LAMPLoadProfile &lamp = getAnalysis< LAMPLoadProfile >();
    //LampOracle lampaa(&lamp);
    //lampaa.InitializeLoopAA(this, mod.getDataLayout());
    SmtxSpeculationManager &smtxMan = getAnalysis<SmtxSpeculationManager>();
    SmtxAA smtxaa(&smtxMan, perf);
    smtxaa.InitializeLoopAA(this, mod.getDataLayout());

    // Points-to
    const Read &spresults = getAnalysis< ReadPass >().getProfileInfo();
    PointsToAA pointstoaa(spresults);
    pointstoaa.InitializeLoopAA(this, mod.getDataLayout());
    // Value predictions
    ProfileGuidedPredictionSpeculator &predspec = getAnalysis< ProfileGuidedPredictionSpeculator >();
    PredictionAA predaa(&predspec, perf);
    predaa.InitializeLoopAA(this, mod.getDataLayout());

    // Ptr-residue
    PtrResidueSpeculationManager &ptrResMan =
        getAnalysis<PtrResidueSpeculationManager>();
    PtrResidueAA ptrresaa(mod.getDataLayout(), ptrResMan, perf);
    ptrresaa.InitializeLoopAA(this, mod.getDataLayout());

    // leave IO to be shared
    //TXIOAA txioaa;
    //txioaa.InitializeLoopAA(this, mod.getDataLayout());

    //CommutativeLibsAA commlibsaa;
    //commlibsaa.InitializeLoopAA(this, mod.getDataLayout());

    SimpleAA simpleaa;
    simpleaa.InitializeLoopAA(this, mod.getDataLayout());

    KillFlow_CtrlSpecAware *killflow_aware =
        &getAnalysis<KillFlow_CtrlSpecAware>();
    CallsiteDepthCombinator_CtrlSpecAware *callsite_aware =
        &getAnalysis<CallsiteDepthCombinator_CtrlSpecAware>();

    // Run on each loop.
    for(Targets::iterator i=targets.begin(mloops), e=targets.end(mloops); i!=e; ++i) {
      Loop *loop = *i;
      ctrlspec->setLoopOfInterest(loop->getHeader());
      predaa.setLoopOfInterest(loop);
      killflow_aware->setLoopOfInterest(ctrlspec, loop);
      callsite_aware->setLoopOfInterest(ctrlspec, loop);
      TIME("Classify loop", runOnLoop(loop));
    }

    // All the added AAs remove themselves from
    // the stack automatically as they are
    // destructed HERE.

    killflow_aware->setLoopOfInterest(nullptr, nullptr);
  }
  return false;
}




static const Ctx *translateContexts(const Read &spresults, const Ctx *root, const Context &context)
{
  // Translate a callsite-search ctx into a spec-priv ctx
  const Ctx *cc = root;
  const CallsiteContext *csc = context.front();
  while( csc )
  {
    cc = spresults.getCtx( csc->getFunction(), cc );
    csc = csc->getParent();
  }

  return cc;
}


static bool intersect_into(const AUs &a, const AUs &b, AUs &out)
{
  const unsigned size_in = out.size();

  // Intersection, ignoring NULL
  for(AUs::const_iterator i=a.begin(), e=a.end(); i!=e; ++i)
  {
    AU *au1 = *i;
    if( au1->type == AU_Null )
      continue;

    for(AUs::const_iterator j=b.begin(), f=b.end(); j!=f; ++j)
    {
      AU *au2 = *j;
      if( au2->type == AU_Null )
        continue;

      if( (*au1) == (*au2) )
      {
        // We only want to report it if we haven't already added it.
        if( std::find(out.begin() /*+size_in*/, out.end(), au1) != out.end() )
          continue;

        out.push_back( au1 );
      }
    }
  }

  return (out.size() > size_in);
}

static void union_into(const ReduxAUs &a, AUs &out)
{
  for(ReduxAUs::const_iterator i=a.begin(), e=a.end(); i!=e; ++i)
  {
    AU *au = i->first;
    out.push_back(au);
  }
}

static void union_into(const AUs &a, AUs &out)
{
  for(AUs::const_iterator i=a.begin(), e=a.end(); i!=e; ++i)
  {
    AU *au = *i;
    out.push_back(au);
  }
}

static void union_into(const AUs &a, HeapAssignment::AUSet &out) {
  for(AUs::const_iterator i=a.begin(), e=a.end(); i!=e; ++i)
  {
    AU *au = *i;
    out.insert(au);
  }
}

static void strip_undefined_objects(AUs &out)
{
  for(unsigned i=0; i<out.size(); ++i)
  {
    if( out[i]->type == AU_Undefined )
    {
      REPORT_DUMP(errs() << "N.B. Removed an UNDEFINED object, at my discretion\n");
      std::swap( out[i], out.back() );
      out.pop_back();
      --i;
    }
  }
}

static void strip_undefined_objects(HeapAssignment::AUSet &out)
{
  for(HeapAssignment::AUSet::iterator i=out.begin(); i!=out.end();)
  {
    AU *au = *i;
    if( au->type == AU_Undefined )
    {
      REPORT_DUMP(errs() << "N.B. Removed an UNDEFINED object, at my discretion\n");
      i = out.erase(i);
    }
    else
      ++i;
  }
}

static void strip_undefined_objects(HeapAssignment::ReduxAUSet &out)
{
  for(HeapAssignment::ReduxAUSet::iterator i=out.begin(); i!=out.end();)
  {
    AU *au = i->first;
    if( au->type == AU_Undefined )
    {
      REPORT_DUMP(errs() << "N.B. Removed an UNDEFINED object, at my discretion\n");
      i = out.erase(i);
    }
    else
      ++i;
  }
}

bool Classify::getLoopCarriedAUs(Loop *loop, const Ctx *ctx, AUs &aus,
                                 HeapAssignment::AUToRemeds &auToRemeds) const {
  KillFlow &kill = getAnalysis< KillFlow >();
  PureFunAA &pure = getAnalysis< PureFunAA >();
  SemiLocalFunAA &semi = getAnalysis< SemiLocalFunAA >();
  ControlSpeculation *ctrlspec = getAnalysis< ProfileGuidedControlSpeculator >().getControlSpecPtr();
  ctrlspec->setLoopOfInterest(loop->getHeader());

  for(Loop::block_iterator i=loop->block_begin(), e=loop->block_end(); i!=e; ++i)
  {
    BasicBlock *srcbb = *i;
    if( ctrlspec->isSpeculativelyDead(srcbb) )
      continue;

    for(BasicBlock::iterator j=srcbb->begin(), f=srcbb->end(); j!=f; ++j)
    {
      Instruction *src = &*j;
      if( !src->mayWriteToMemory() )
        continue;

      // Re-use this to speed it all up.
      ReverseStoreSearch search_src(src, kill, 0, 0, &pure, &semi);

      for(Loop::block_iterator k=loop->block_begin(); k!=e; ++k)
      {
        BasicBlock *dstbb = *k;
        if( ctrlspec->isSpeculativelyDead(dstbb) )
          continue;

        for(BasicBlock::iterator l=dstbb->begin(), h=dstbb->end(); l!=h; ++l)
        {
          Instruction *dst = &*l;
          if( !dst->mayReadFromMemory() )
            continue;

          // There may be a cross-iteration flow from src to dst.
          if (!getUnderlyingAUs(loop, search_src, src, ctx, dst, ctx, aus,
                                auToRemeds))
            return false;
        }
      }
    }
  }
  return true;
}

// Case where one or both instructions are an aggregation of
// several operations.  In this case, we want to know specifically
// which instructions caused the dependence, not assume that
// all did.
bool Classify::getUnderlyingAUs(Loop *loop, ReverseStoreSearch &search_src,
                                Instruction *src, const Ctx *src_ctx,
                                Instruction *dst, const Ctx *dst_ctx, AUs &aus,
                                HeapAssignment::AUToRemeds &auToRemeds) const {
  KillFlow &kill = getAnalysis< KillFlow >();
  PureFunAA &pure = getAnalysis< PureFunAA >();
  SemiLocalFunAA &semi = getAnalysis< SemiLocalFunAA >();

  Remedies R;
  CCPairs flows;
  CCPairsRemedsMap remedNoFlows;
  CallsiteDepthCombinator::doFlowSearchCrossIter(
      src, dst, loop, search_src, kill, R, &flows, 0, 0, &remedNoFlows, &pure, &semi);

  //if( flows.empty() )
  //  return true;

  ControlSpeculation *ctrlspec = getAnalysis< ProfileGuidedControlSpeculator >().getControlSpecPtr();
  ctrlspec->setLoopOfInterest(loop->getHeader());

  for (CCPairs::const_iterator i = flows.begin(), e = flows.end(); i != e;
       ++i) {
    const CtxInst srcp = i->first;
    const CtxInst dstp = i->second;

    if( ctrlspec->isSpeculativelyDead(srcp) )
      continue;
    if( ctrlspec->isSpeculativelyDead(dstp) )
      continue;

    if( !getUnderlyingAUs(srcp,src_ctx, dstp,dst_ctx, aus) )
      return false;
  }

  for (auto i : remedNoFlows) {
    const CtxInst srcp = i.first.first;
    const CtxInst dstp = i.first.second;

    // find underlying AUs and populate their needed remedies for no LC flows

    AUs relAUs;

    if (!getUnderlyingAUs(srcp, src_ctx, dstp, dst_ctx, relAUs, false))
      return false;

    for (auto relAU : relAUs) {
      for (auto remed : i.second) {
        auToRemeds[relAU].insert(remed);
      }
    }
  }

  return true;
}

bool Classify::getUnderlyingAUs(const CtxInst &src, const Ctx *src_ctx,
                                const CtxInst &dst, const Ctx *dst_ctx,
                                AUs &aus, bool printDbgFlows) const {
  const Read &spresults = getAnalysis< ReadPass >().getProfileInfo();

  const Ctx *src_cc = translateContexts(spresults, src_ctx, src.getContext());
  const Instruction *srci = src.getInst();

  // Footprint of operation ci
  // It's valid to look only at the WRITE footprint,
  // because we're talking about a flow dep.
  AUs ri, wi;
  ReduxAUs xi;
  if( ! spresults.getFootprint(srci,src_cc,ri,wi,xi) )
  {
    errs() << "Failed to get write footprint for: " << src << '\n';
    return false;
  }

  union_into(xi,wi);

  // We are allowed to do whatever with undefined behavior.
  strip_undefined_objects(wi);

  if( wi.empty() )
    return true;

  const Ctx *dst_cc = translateContexts(spresults, dst_ctx, dst.getContext());
  const Instruction *dsti = dst.getInst();;

  AUs rj, wj;
  ReduxAUs xj;
  if( !spresults.getFootprint(dsti,dst_cc,rj,wj,xj) )
  {
    errs() << "Failed to get read footprint for: " << dst << '\n';
    return false;
  }

  // rj ||= xj
  union_into(xj, rj);

  // We are allowed to do whatever with undefined behavior.
  // I choose to remove them.
  strip_undefined_objects(rj);

  if( rj.empty() )
    return true;

  const unsigned size_before = aus.size();
  intersect_into(wi, rj, aus);

  // Print the new AUs.
  REPORT_DUMP(
    if (printDbgFlows) {
    const unsigned N = aus.size();
    if (N > size_before) {
      bool first = true;
      for (unsigned i = size_before; i < N; ++i) {
        AU *au1 = aus[i];

        if (first)
          errs() << "( ";
        else
          errs() << ", ";

        errs() << *au1;
        first = false;
      }

      errs() << " )\n"
             << "There is a flow from:\n"
             << src << "\nto:\n"
             << dst << "\n\n";
    }
  });

  return true;
}

static BasicBlock *getLoopEntryBB(const Loop *loop) {
  BasicBlock *header = loop->getHeader();
  BranchInst *term = dyn_cast<BranchInst>(header->getTerminator());
  BasicBlock *headerSingleInLoopSucc = nullptr;
  if (term) {
    for (unsigned sn = 0; sn < term->getNumSuccessors(); ++sn) {
      BasicBlock *succ = term->getSuccessor(sn);
      if (loop->contains(succ)) {
        if (headerSingleInLoopSucc) {
          headerSingleInLoopSucc = nullptr;
          break;
        } else
          headerSingleInLoopSucc = succ;
      }
    }
  }
  return headerSingleInLoopSucc;
}

static bool isTransLoopInvariant(const Value *val, const Loop *L) {
  if (L->isLoopInvariant(val))
    return true;

  if (auto inst = dyn_cast<Instruction>(val)) {

    // limit to only arithmetic/logic ops
    if (!inst->isBinaryOp() && !inst->isCast() && !inst->isLogicalShift() &&
        !inst->isArithmeticShift() && !inst->isBitwiseLogicOp())
      return false;

    for (unsigned i = 0; i < inst->getNumOperands(); ++i) {
      if (!isTransLoopInvariant(inst->getOperand(i), L))
        return false;
    }
    return true;
  }
  return false;
}

static bool isLoopInvariantValue(const Value *V, const Loop *L) {
  if (L->isLoopInvariant(V)) {
    return true;
  } else if (isTransLoopInvariant(V, L)) {
    return true;
  } else if (const GlobalValue *globalSrc = liberty::findGlobalSource(V)) {
    return isLoopInvariantGlobal(globalSrc, L);
  } else
    return false;
}

static bool extractValuesInSCEV(const SCEV *scev,
                                std::unordered_set<const Value *> &involvedVals,
                                ScalarEvolution *se) {
  if (!scev)
    return false;

  if (auto unknown = dyn_cast<SCEVUnknown>(scev)) {
    involvedVals.insert(unknown->getValue());
    return true;
  } else if (isa<SCEVConstant>(scev))
    return true;
  else if (auto *cast = dyn_cast<SCEVCastExpr>(scev))
    return extractValuesInSCEV(cast->getOperand(), involvedVals, se);
  else if (auto nary = dyn_cast<SCEVNAryExpr>(scev)) {
    for (unsigned i = 0; i < nary->getNumOperands(); ++i) {
      if (!extractValuesInSCEV(nary->getOperand(i), involvedVals, se))
        return false;
    }
    return true;
  } else if (auto udiv = dyn_cast<SCEVUDivExpr>(scev)) {
    if (!extractValuesInSCEV(udiv->getLHS(), involvedVals, se))
      return false;
    return extractValuesInSCEV(udiv->getRHS(), involvedVals, se);
  } else if (isa<SCEVCouldNotCompute>(scev))
    return false;
  else
    // if any other type of SCEV is introduced, conservatively return false
    return false;
}

static bool isLoopInvariantSCEV(const SCEV *scev, const Loop *L,
                                ScalarEvolution *se) {
  if (se->isLoopInvariant(scev, L))
    return true;
  std::unordered_set<const Value *> involvedVals;
  bool success = extractValuesInSCEV(scev, involvedVals, se);
  if (!success)
    return false;
  bool allLoopInvariant = true;
  for (auto val : involvedVals) {
    allLoopInvariant &= isLoopInvariantValue(val, L);
    if (!allLoopInvariant)
      break;
  }
  return allLoopInvariant;
}

bool Classify::getNoFullOverwritePrivAUs(
    Loop *loop, const Ctx *ctx, HeapAssignment::AUSet &aus,
    HeapAssignment::AUSet &wawDepAUs,
    HeapAssignment::AUToRemeds &noWAWRemeds) const {
  KillFlow &kill = getAnalysis<KillFlow>();
  ControlSpeculation *ctrlspec =
      getAnalysis<ProfileGuidedControlSpeculator>().getControlSpecPtr();
  ctrlspec->setLoopOfInterest(loop->getHeader());

  for (Loop::block_iterator i = loop->block_begin(), e = loop->block_end();
       i != e; ++i) {
    BasicBlock *srcbb = *i;
    if (ctrlspec->isSpeculativelyDead(srcbb))
      continue;

    for (BasicBlock::iterator j = srcbb->begin(), f = srcbb->end(); j != f;
         ++j) {
      Instruction *src = &*j;
      if (!src->mayWriteToMemory())
        continue;

      for (Loop::block_iterator k = loop->block_begin(); k != e; ++k) {
        BasicBlock *dstbb = *k;
        if (ctrlspec->isSpeculativelyDead(dstbb))
          continue;

        for (BasicBlock::iterator l = dstbb->begin(), h = dstbb->end(); l != h;
             ++l) {
          Instruction *dst = &*l;
          if (!dst->mayWriteToMemory())
            continue;

          // check if WAW from src to dst is full-overwrite
          if (!getNoFullOverwritePrivAUs(src, dst, loop, aus, wawDepAUs,
                                         noWAWRemeds, kill))
            return false;
        }
      }
    }
  }
  return true;
}

static const Value *getIPtr(const Instruction *I) {
  const Value *ptr = liberty::getMemOper(I);

  if (!ptr) {
    if (const MemTransferInst *mti = dyn_cast<MemTransferInst>(I)) {
      ptr = mti->getRawDest();
    }
  }
  return ptr;
}

static bool noFullOverwrite(const AUs auWriteUnion,
                            HeapAssignment::AUSet &aus) {
  union_into(auWriteUnion, aus);

  //for (auto au: auWriteUnion)
  //  errs() << "noFullOverwrite: " << *au->value << "\n";

  return true;
}

bool Classify::getNoFullOverwritePrivAUs(
    const Instruction *A, const Instruction *B, const Loop *L,
    HeapAssignment::AUSet &aus, HeapAssignment::AUSet &wawDepAUs,
    HeapAssignment::AUToRemeds &noWAWRemeds, KillFlow &kill) const {

  LoopAA *top = kill.getTopAA();

  const Read &spresults = getAnalysis<ReadPass>().getProfileInfo();
  const Ctx *ctx = spresults.getCtx(L);

  AUs auWriteUnion;
  if (!getUnderlyingAUs(A, ctx, B, ctx, spresults, auWriteUnion))
    return false;

  if (auWriteUnion.empty())
    return true;

  Remedies R;

  // TODO: maybe avoid expensive remedies
  // use of points-to can be allowed with smart subheaping
  // check for WAW dep
  if (Remediator::noMemoryDep(A, B, LoopAA::Before, LoopAA::After, L, top,
                              false, true, R)) {
    for (auto au : auWriteUnion) {
      for (auto remed : R) {
        noWAWRemeds[au].insert(remed);
      }
    }
    return true;
  }

  // the auWriteUnion participate in WAW deps. collect them
  union_into(auWriteUnion, wawDepAUs);

  const Value *ptrA = getIPtr(A);

  ModuleLoops &mloops = getAnalysis< ModuleLoops >();
  Function *f = L->getHeader()->getParent();
  PostDominatorTree *pdt = &mloops.getAnalysis_PostDominatorTree(f);
  LoopInfo *li = &mloops.getAnalysis_LoopInfo(f);
  ScalarEvolution *se = &mloops.getAnalysis_ScalarEvolution(f);

  // check for full-overlap priv
  //
  // need to be loop-carried WAW (two stores involved) where the privitizable
  // store is either A or B
  if (isa<StoreInst>(A) && isa<StoreInst>(B)) {

    if (A != B) {
      // want to cover cases such as this one:
      // for (..)  {
      //   x = ...   (B)
      //   ...
      //   x = ...   (A)
      // }
      // In this case self-WAW of A is killed by B and vice-versa but there is
      // still a LC WAW from A to B. This LC WAW can be ignored provided that B
      // overwrites the memory footprint of A. If not then it is likely that
      // there is no self-WAW LC dep but there is a real non-full-overwrite WAW
      // from A to B. Covariance benchmark from Polybench exhibits this case
      // (A: symmat[j2][j1] = ...  , B: symmat[j1][j2] = 0.0;).
      // Correlation exhibits a similar issue.
      //
      // check that A is killed by B

      if (!L)
        return noFullOverwrite(auWriteUnion, aus);

      if (!ptrA)
        return noFullOverwrite(auWriteUnion, aus);

      const BasicBlock *bbA = A->getParent();
      const BasicBlock *bbB = B->getParent();
      // dominance info are intra-procedural
      if (bbA->getParent() != bbB->getParent())
        return noFullOverwrite(auWriteUnion, aus);
      const DominatorTree *dt = kill.getDT(bbA->getParent());

      // collect the chain of all idom from A
      DomTreeNode *nodeA = dt->getNode(const_cast<BasicBlock *>(bbA));
      DomTreeNode *nodeB = dt->getNode(const_cast<BasicBlock *>(bbB));
      if (!nodeA || !nodeB)
        return noFullOverwrite(auWriteUnion, aus);

      std::unordered_set<DomTreeNode *> idomChainA;
      for (DomTreeNode *n = nodeA; n; n = n->getIDom()) {
        const BasicBlock *bb = n->getBlock();
        if (!bb || !L->contains(bb))
          break;
        idomChainA.insert(n);
      }

      const BasicBlock *commonDom = nullptr;
      DomTreeNode *commonDomNode = nullptr;
      for (DomTreeNode *n = nodeB; n; n = n->getIDom()) {
        const BasicBlock *bb = n->getBlock();
        if (!bb || !L->contains(bb))
          break;
        if (idomChainA.count(n)) {
          commonDom = bb;
          commonDomNode = n;
          break;
        }
      }
      if (!commonDom)
        return noFullOverwrite(auWriteUnion, aus);

      if (commonDom == B->getParent()) {
        if (kill.instMustKill(B, ptrA, 0, 0, L)) {
          return true;
        } else {
          commonDomNode = commonDomNode->getIDom();
          commonDom = commonDomNode->getBlock();
          if (!commonDom || !L->contains(commonDom))
            return noFullOverwrite(auWriteUnion, aus);
        }
      }

      if (!kill.blockMustKill(commonDom, ptrA, nullptr, A, 0, 0, L))
        return noFullOverwrite(auWriteUnion, aus);

      // the following check if not enough for correlation
      // if (!kill.pointerKilledBefore(L, ptrA, A) &&
      //    !kill.pointerKilledBefore(L, ptrB, A))
      //  return lookForCheaperNoModRef(A, ptrA, rel, B, nullptr, 0, L, R, tmpR,
      //  privA);
      // the following check is too conservative and misses fullOverlap
      // opportunities. Need to use killflow
      // if (!isPointerKillBefore(L, ptrA, A, true))
      //  return lookForCheaperNoModRef(A, ptrA, rel, B, nullptr, 0, L, R, tmpR,
      //  privA);

      // treat it as full_overlap. if it is not a fullOverlap there will be
      // self-WAW for either A or B that will not be reported as FullOverlap and
      // the underlying AUs will remain in the private family

      return true;
    }

    // A == B
    const StoreInst *privStore = dyn_cast<StoreInst>(A);

    // evaluate if the private store overwrites the same memory locations and
    // executes the same number of times for every iteration of the loop of
    // interest. If full-overlap is proved for this write then assign
    // PrivRemedy::FullOverlap type to the remedy.

    // ensure that on every iter of loop of interest the private store will
    // execute the same number of times.
    //
    // the most accurate approach would be to explore all the ctrl deps until
    // you reach the branch of the header of the loop of interest.
    //
    // for now we use a more conservative but a bit simpler approach.
    // check if the private store executes on every iter of the loop of interest
    // or in every iter of an inner loop which executes on every iter of outer
    // loop. This approach address only loop depth of 1 and does not take
    // advantage of loop-invariant if-statements; still in practise is very
    // common. Note also that this approach requires that all involved BBs are
    // in the same function (postdominator is intraprocedural)

    const BasicBlock *loopEntryBB = getLoopEntryBB(L);
    if (!loopEntryBB)
      return noFullOverwrite(auWriteUnion, aus);

    if (loopEntryBB->getParent() != privStore->getFunction())
      return noFullOverwrite(auWriteUnion, aus);
    const Loop *innerLoop = nullptr;
    if (pdt->dominates(privStore->getParent(), loopEntryBB)) {
      // private store executes on every iter of loop of interest
    } else {
      // private store does not postdominate loop of interest entry BB.
      // check if private store executes on every iter of an inner loop and the
      // header of inner loop executes on every iter of its parent loop. check
      // same properties on every parent loop until the loop of interest is
      // reached.
      innerLoop = li->getLoopFor(privStore->getParent());
      if (!innerLoop)
        return noFullOverwrite(auWriteUnion, aus);
      if (innerLoop->getHeader()->getParent() != loopEntryBB->getParent())
        return noFullOverwrite(auWriteUnion, aus);
      // check that store executes on every iter of inner loop
      const BasicBlock *innerLoopEntryBB = getLoopEntryBB(innerLoop);
      if (!innerLoopEntryBB)
        return noFullOverwrite(auWriteUnion, aus);
      if (!pdt->dominates(privStore->getParent(), innerLoopEntryBB))
        return noFullOverwrite(auWriteUnion, aus);
      // check that the inner loop that contains the store is a subloop of the
      // loop of interest
      if (!L->contains(innerLoop))
        return noFullOverwrite(auWriteUnion, aus);

      // go over all the parent loops until the loop of interest is reached
      const Loop *parentL = innerLoop->getParentLoop();
      const Loop *childL = innerLoop;
      do {
        if (!parentL)
          return noFullOverwrite(auWriteUnion, aus);
        const BasicBlock *parLEntryBB = getLoopEntryBB(parentL);
        if (!parLEntryBB)
          return noFullOverwrite(auWriteUnion, aus);
        if (childL->getHeader()->getParent() != parLEntryBB->getParent())
          return noFullOverwrite(auWriteUnion, aus);
        if (!pdt->dominates(childL->getHeader(), parLEntryBB))
          return noFullOverwrite(auWriteUnion, aus);
        const Loop *tmpL = parentL;
        parentL = parentL->getParentLoop();
        childL = tmpL;
      } while (childL != L);
    }

    // check if address of store is either a loop-invariant (to the loop of
    // interest), or a gep with only constant or affine SCEVAddRecExpr (to loop
    // with loop-invariant trip counts) indices
    //
    const Value *ptrPrivStore = privStore->getPointerOperand();
    const Loop *scevLoop = nullptr;
    if (L->isLoopInvariant(ptrPrivStore)) {
      // good
    } else if (isa<GlobalValue>(ptrPrivStore)) {
      // good
    } else if (isa<GetElementPtrInst>(ptrPrivStore)) {
      const GetElementPtrInst *gep = dyn_cast<GetElementPtrInst>(ptrPrivStore);

      // the base pointer of the gep should be loop-invariant (no support
      // yet for 2D arrays etc.)
      if (!isLoopInvariantValue(gep->getPointerOperand(), L))
        return noFullOverwrite(auWriteUnion, aus);

      // traverse all the indices of the gep, make sure that they are all
      // constant or affine SCEVAddRecExpr (to loops with loop-invariant trip
      // counts, and with loop-invariant step, start and limit/max_val).
      for (auto idx = gep->idx_begin(); idx != gep->idx_end(); ++idx) {
        const Value *idxV = *idx;
        if (L->isLoopInvariant(idxV))
          continue;
        else if (isTransLoopInvariant(idxV, L))
          continue;
        else if (se->isSCEVable(idxV->getType())) {
          if (const SCEVAddRecExpr *addRec = dyn_cast<SCEVAddRecExpr>(
                  se->getSCEV(const_cast<Value *>(idxV)))) {
            if (!addRec)
              return noFullOverwrite(auWriteUnion, aus);
            if (!addRec->isAffine())
              return noFullOverwrite(auWriteUnion, aus);

            if (scevLoop && scevLoop != addRec->getLoop())
              return noFullOverwrite(auWriteUnion, aus);

            scevLoop = addRec->getLoop();
            // if (scevLoop == L || !L->contains(scevLoop))
            if (scevLoop != innerLoop)
              return noFullOverwrite(auWriteUnion, aus);

            // check for loop-invariant offset from base pointer (start, step
            // and loop trip count)

            if (!se->hasLoopInvariantBackedgeTakenCount(scevLoop))
              return noFullOverwrite(auWriteUnion, aus);

            if (!isLoopInvariantSCEV(addRec->getStart(), L, se) ||
                !isLoopInvariantSCEV(addRec->getStepRecurrence(*se), L, se))
              return noFullOverwrite(auWriteUnion, aus);

          } else if (isa<SCEVUnknown>(se->getSCEV(const_cast<Value *>(idxV)))) {
            // detect pseudo-canonical IV (0, +, 1) and return max value
            auto limit = getLimitUnknown(idxV, innerLoop);
            if (!innerLoop || !limit || !isLoopInvariantValue(limit, L))
              return noFullOverwrite(auWriteUnion, aus);
          }

        } else
          return noFullOverwrite(auWriteUnion, aus);
      }
    } else
      return noFullOverwrite(auWriteUnion, aus);

    // success. private store executes same number of times on every loop of
    // interest iter
    return true;
  }

  return noFullOverwrite(auWriteUnion, aus);
}

bool Classify::getUnderlyingAUs(const Instruction *srci, const Ctx *src_ctx,
                                const Instruction *dsti, const Ctx *dst_ctx,
                                const Read &spresults, AUs &aus) const {

  // Footprint of operation ci
  // It's valid to look only at the WRITE footprint,
  // because we're talking about a flow dep.
  AUs ri, wi;
  ReduxAUs xi;
  if (!spresults.getFootprint(srci, src_ctx, ri, wi, xi)) {
    errs() << "Failed to get write footprint for: " << *srci << '\n';
    return false;
  }

  union_into(xi, wi);

  // We are allowed to do whatever with undefined behavior.
  strip_undefined_objects(wi);

  if (wi.empty())
    return true;

  AUs rj, wj;
  ReduxAUs xj;
  if (!spresults.getFootprint(dsti, dst_ctx, rj, wj, xj)) {
    errs() << "Failed to get read footprint for: " << *dsti << '\n';
    return false;
  }

  // rj ||= xj
  union_into(xj, wj);

  // We are allowed to do whatever with undefined behavior.
  // I choose to remove them.
  strip_undefined_objects(wj);

  if (wj.empty())
    return true;

  union_into(wi, aus);
  union_into(wj, aus);

  return true;
}

bool HeapAssignment::isLocalPrivateStackAU(const Value *V, const Loop *L) {
  if (const AllocaInst *alloca = dyn_cast<AllocaInst>(V)) {
    // find uses of alloca by @llvm.lifetime.start and lifetime.end
    const IntrinsicInst *lifetimeStart = nullptr;
    const IntrinsicInst *lifetimeEnd = nullptr;
    for (auto j = alloca->user_begin(); j != alloca->user_end(); ++j) {
      if (auto userI = dyn_cast<IntrinsicInst>(*j)) {
        Intrinsic::ID ID = userI->getIntrinsicID();
        if (ID == Intrinsic::lifetime_start)
          lifetimeStart = userI;
        else if (ID == Intrinsic::lifetime_end)
          lifetimeEnd = userI;
      } else if (auto userI = dyn_cast<BitCastInst>(*j)) {
        for (auto k = userI->user_begin(); k != userI->user_end(); ++k) {
          if (auto kI = dyn_cast<IntrinsicInst>(*k)) {
            Intrinsic::ID ID = kI->getIntrinsicID();
            if (ID == Intrinsic::lifetime_start)
              lifetimeStart = kI;
            else if (ID == Intrinsic::lifetime_end)
              lifetimeEnd = kI;
          }
        }
      }
    }
    if (!lifetimeStart || !lifetimeEnd)
      return false;

    // check that the lifetime.start and lifetime.end calls are within the loop
    if (!L->contains(lifetimeStart) || !L->contains(lifetimeEnd))
      return false;

    REPORT_DUMP(errs() << "Alloca found to be local: " << *alloca << "\n");
    return true;
  }
  return false;
}

bool HeapAssignment::isLocalPrivateGlobalAU(const Value *ptr, const Loop *L) {
  if (const GlobalValue *gv = dyn_cast<GlobalValue>(ptr)) {
    // if global variable is not used outside the loop then it is a local
    if (isGlobalLocalToLoop(gv, L)) {
      REPORT_DUMP(errs() << "Global found to be local: " << *gv << "\n");
      return true;
    }
  }
  return false;
}

bool Classify::runOnLoop(Loop *loop)
{
  BasicBlock *header = loop->getHeader();
  Function *fcn = header->getParent();

  const Read &spresults = getAnalysis< ReadPass >().getProfileInfo();
  if( !spresults.resultsValid() )
    return false;

  REPORT_DUMP(errs() << "***************** Classify: "
    << fcn->getName() << " :: " << header->getName()
    << " *****************\n");

  ControlSpeculation *ctrlspec = getAnalysis< ProfileGuidedControlSpeculator >().getControlSpecPtr();
  ctrlspec->setLoopOfInterest(loop->getHeader());
  if( ctrlspec->isNotLoop(loop) )
  {
    REPORT_DUMP(errs() << "- This loop never takes its backedge.\n");
    return false;
  }

  // Build the assignment
  HeapAssignment &assignment = assignments[header];
  HeapAssignment::AUSet
        &sharedAUs = assignment.getSharedAUs(),
        &localAUs = assignment.getLocalAUs(),
        &privateAUs = assignment.getPrivateAUs(),
        &killPrivAUs = assignment.getKillPrivAUs(),
        &shareablePrivAUs = assignment.getSharePrivAUs(),
        &readOnlyAUs = assignment.getReadOnlyAUs();
  HeapAssignment::AUToRemeds &cheapPrivAUs = assignment.getCheapPrivAUs();
  HeapAssignment::ReduxAUSet &reductionAUs = assignment.getReductionAUs();
  HeapAssignment::AUToRemeds &noWAWRemeds = assignment.getNoWAWRemeds();

  const Ctx *ctx = spresults.getCtx(loop);

  // Find all AUs which are read,written,reduced...
  AUs reads, writes;
  ReduxAUs reductions;
  if( !spresults.getFootprint(loop, ctx, reads, writes, reductions) )
  {
    REPORT_DUMP(errs() << "Classify: Failed to get write footprint of loop; abort\n");
    return false;
  }

  // {{{ Debug
  if( PrintFootprints )
  {
    typedef std::set<AU*> AUSet;
    typedef std::set<ReduxAU> ReduxAUSet;

    if( ! reads.empty() )
    {
      AUSet readSet(reads.begin(),reads.end());
      errs() << "---Loop footprint---\n"
             << "  Reads (" << readSet.size() << "):\n";
      for(AUSet::const_iterator i=readSet.begin(), e=readSet.end(); i!=e; ++i)
        errs() << "   " << **i << '\n';
    }

    if( ! writes.empty() )
    {
      AUSet writeSet(writes.begin(),writes.end());
      errs() << "  Writes (" << writeSet.size() << "):\n";
      for(AUSet::const_iterator i=writeSet.begin(), e=writeSet.end(); i!=e; ++i)
        errs() << "   " << **i << '\n';
    }

    if( ! reductions.empty() )
    {
      ReduxAUSet reductionSet( reductions.begin(), reductions.end() );
      errs() << "  Redux (" << reductionSet.size() << "):\n";
      for(ReduxAUSet::const_iterator i=reductionSet.begin(), e=reductionSet.end(); i!=e; ++i)
        errs() << "   " << i->second << " . " << *(i->first) << '\n';
    }

    errs() << "---End footprint---\n";
  }
  // }}}

  // Find local AUs first.
  for(AUs::const_iterator i=writes.begin(), e=writes.end(); i!=e; ++i)
  {
    AU *au = *i;

    // Is this AU local?
    const Read::Ctx2Count &locals = spresults.find_locals(au);
    for(Read::Ctx2Count::const_iterator j=locals.begin(), f=locals.end(); j!=f; ++j)
      if( j->first->matches(ctx) )
      {
        localAUs.insert(au);
        break;
      }
  }

  // reductionAUs = reductions \ locals
  // Eliminate inconsistent reductions
  HeapAssignment::AUSet inconsistent;
  for(ReduxAUs::iterator i=reductions.begin(), e=reductions.end(); i!=e; ++i)
  {
    AU *au = i->first;
    if( inconsistent.count( au ) )
      continue;
    if( localAUs.count( au ) )
      continue;

    Reduction::Type rt = i->second;

    HeapAssignment::ReduxAUSet::iterator j=reductionAUs.find(au);
    if( j != reductionAUs.end() && j->second != rt )
    {
      REPORT_DUMP(errs() << "Not redux: au " << *au
                   << " is sometimes " << Reduction::names[rt]
                   << " but other times " << Reduction::names[j->second] << '\n');
      inconsistent.insert(au);
      reductionAUs.erase(j);
    }

    else
      reductionAUs[ au ] = rt;
  }
  // reductionAUs = reductionAUs \ reads
  for(AUs::const_iterator i=reads.begin(), e=reads.end(); i!=e; ++i)
  {
    AU *au = *i;
    if( reductionAUs.count(au) )
    {
      reductionAUs.erase( au );

      // since the reduction entry
      // counted as both a read and a write.
      writes.push_back( au );
    }
  }

  // reductionAUs = reductionAUs \ writes
  for(AUs::const_iterator i=writes.begin(), e=writes.end(); i!=e; ++i)
  {
    AU *au = *i;
    if( reductionAUs.count(au) )
    {
      reductionAUs.erase( au );

      // since the reduction entry counted
      // as both a read and a write
      reads.push_back( au );
    }
  }

  HeapAssignment::AUSet writeSet(writes.begin(), writes.end());

  // AUs which are read, but not written and not local
  for(AUs::const_iterator i=reads.begin(), e=reads.end(); i!=e; ++i)
  {
    AU *au = *i;

    if( localAUs.count(au) )
      continue;
    if( reductionAUs.count(au) )
      continue;

    // Is this AU local?
    bool isLocal = false;
    const Read::Ctx2Count &locals = spresults.find_locals(au);
    for(Read::Ctx2Count::const_iterator j=locals.begin(), f=locals.end(); j!=f; ++j)
      if( j->first->matches(ctx) )
      {
        isLocal = true;
        break;
      }

    if( isLocal )
      localAUs.insert(au);
    else if (!writeSet.count(au))
      readOnlyAUs.insert(au);
  }


  PerformanceEstimator *perf = &getAnalysis< ProfilePerformanceEstimator >();
  // read-only aa
  ReadOnlyAA roaa(spresults, &readOnlyAUs , ctx, perf);
  roaa.InitializeLoopAA(this, fcn->getParent()->getDataLayout());

  // short-lived aa
  ShortLivedAA localaa(spresults, &localAUs, ctx, perf);
  localaa.InitializeLoopAA(this, fcn->getParent()->getDataLayout());

  // For each pair (write, read) in the loop.
  AUs loopCarried;
  HeapAssignment::AUToRemeds auToRemeds;
  if( !getLoopCarriedAUs(loop, ctx, loopCarried, auToRemeds) )
  {
    REPORT_DUMP(errs() << "Wild object spoiled classification.\n");
    return false;
  }

  for(AUs::const_iterator i=loopCarried.begin(), e=loopCarried.end(); i!=e; ++i)
    if( !localAUs.count( *i ) )
      if( !reductionAUs.count( *i ) )
        sharedAUs.insert( *i );

  // AUs which are written during the loop, but which
  // are not local, shared or reduction, are privatized.
  for(AUs::const_iterator i=writes.begin(), e=writes.end(); i!=e; ++i)
  {
    AU *au = *i;

    // Is this AU local, shared or redux?
    if( localAUs.count(au) )
      continue;
    if( sharedAUs.count(au) )
      continue;
    if( reductionAUs.count(au) )
      continue;

    // Otherwise, it is private.
    privateAUs.insert(au);

    if (!auToRemeds.count(au)) {
      Remedies R;
      cheapPrivAUs[au] = R;
    } else
      cheapPrivAUs[au] = auToRemeds[au];
  }

  // TODO: create a separate heap for privLocals.
  // Cannot be with regular locals (not freeing).
  //
  // detect stack locals
  auto aui = privateAUs.begin();
  while (aui != privateAUs.end()) {
    auto au = *aui;
    if (!au->value)
    {
      ++aui;
      continue;
    }
    if (HeapAssignment::isLocalPrivateStackAU(au->value, loop)) {
      killPrivAUs.insert(au);
      aui = privateAUs.erase(aui);
      cheapPrivAUs.erase(au);
    }
    else
      ++aui;
  }

  // remove the expensive to remedy private aus from the cheap set
  auto it = cheapPrivAUs.begin();
  while (it != cheapPrivAUs.end()) {
    auto i = *it;
    if (LoopAA::containsExpensiveRemeds(i.second)) {
      it = cheapPrivAUs.erase(it);
    }
    else {
      ++it;
    }
  }

  // detect global local privates
  it = cheapPrivAUs.begin();
  while (it != cheapPrivAUs.end()) {
    auto i = *it;
    AU *au = i.first;
    if (!au->value) {
      ++it;
      continue;
    }
    if (HeapAssignment::isLocalPrivateGlobalAU(au->value, loop)) {
      killPrivAUs.insert(au);
      privateAUs.erase(au);
      it = cheapPrivAUs.erase(it);
    }
    else {
      ++it;
    }
  }

  // find full-overlap-private objects (assigned to killpriv)
  HeapAssignment::AUSet noFullOverwriteAUs;
  HeapAssignment::AUSet wawDepAUs;
  if (!getNoFullOverwritePrivAUs(loop, ctx, noFullOverwriteAUs, wawDepAUs,
                                 noWAWRemeds)) {
    REPORT_DUMP(errs() << "Wild object spoiled classification.\n");
    return false;
  }

  //TODO: need to take those into account when using killpriv and shareable

  for (auto i : cheapPrivAUs) {
    AU *au = i.first;
    if (!noFullOverwriteAUs.count(au) && wawDepAUs.count(au)) {
      killPrivAUs.insert(au);
      privateAUs.erase(au);
    } else if (!wawDepAUs.count(au)) {
      shareablePrivAUs.insert(au);
      privateAUs.erase(au);
    }
  }

  /*
  // AUs which are read, but which are not
  // local, shared, reduction are read-only.
  for(AUs::const_iterator i=reads.begin(), e=reads.end(); i!=e; ++i)
  {
    AU *au = *i;

    if( localAUs.count(au) )
      continue;
    else if( sharedAUs.count(au) )
      continue;
    if( reductionAUs.count(au) )
      continue;
    else if( privateAUs.count(au) )
      continue;

    // Is this AU local?
    bool isLocal = false;
    const Read::Ctx2Count &locals = spresults.find_locals(au);
    for(Read::Ctx2Count::const_iterator j=locals.begin(), f=locals.end(); j!=f; ++j)
      if( j->first->matches(ctx) )
      {
        isLocal = true;
        break;
      }

    if( isLocal )
      localAUs.insert(au);
    else
      readOnlyAUs.insert(au);
  }
  */

  // Strip the undefined AU
  strip_undefined_objects( sharedAUs );
  strip_undefined_objects( localAUs );
  strip_undefined_objects( privateAUs );
  strip_undefined_objects( readOnlyAUs );
  strip_undefined_objects( reductionAUs );
  strip_undefined_objects( killPrivAUs );
  strip_undefined_objects( shareablePrivAUs );

  assignment.assignSubHeaps();

  assignment.setValidFor(loop);
  ++numClassified;
  REPORT_DUMP( errs() << assignment );

  return false;
}

template <class In>
static AU *getAU(In in)
{
  return 0;
}

template <>
AU *getAU<AU*>(AU *in)
{
  return in;
}

template <>
AU *getAU<HeapAssignment::ReduxAUSet::value_type&>(HeapAssignment::ReduxAUSet::value_type &pair)
{
  return pair.first;
}

template <class Collection>
void HeapAssignment::assignSubHeaps(Collection &c)
{
  // I don't yet understand this opportunity fully,
  // and so I'll invent a heuristic.
  // The design constraints of this heuristic
  // are:
  //  - Repeatability: if we run the compiler several
  //    times, we want the same sub-heap assignment.
  //
  // The heuristic is:
  // - (Hash the object's name) mod N.
  for(typename Collection::iterator i=c.begin(), e=c.end(); i!=e; ++i)
  {
    const AU *au = getAU(*i);
    stable_hash_code h = stable_hash(au);
    setSubHeap(au, h % NumSubHeaps);
  }
}

void HeapAssignment::assignSubHeaps()
{
  // Assign objects to sub-heaps.
  assignSubHeaps( getSharedAUs() );
  assignSubHeaps( getLocalAUs() );
  assignSubHeaps( getPrivateAUs() );
  assignSubHeaps( getKillPrivAUs() );
  assignSubHeaps( getSharePrivAUs() );
  // (don't bother with the read-only heap)
  assignSubHeaps( getReductionAUs() );
}

bool Classify::isAssigned(const Loop *L) const
{
  return assignments.count( L->getHeader() );
}

const HeapAssignment &Classify::getAssignmentFor(const Loop *L) const
{
  Loop2Assignments::const_iterator i = assignments.find( L->getHeader() );
  assert( i != assignments.end() && "No assignment available for this loop");

  return i->second;
}

char Classify::ID = 0;
static RegisterPass<Classify> x("spec-priv-classify",
 "Classify all AUs as one of LOCAL, PRIVATE, REDUCTION, SHARED or READ-ONLY", false,false);

HeapAssignment::Type HeapAssignment::join(Type a, Type b)
{
  if( a == b )
    return a;

  // This is a symmetric operator.
  if( a > b )
    std::swap(a,b);

  if( a == ReadOnly && b != Redux )
    return b;

  if( a == Local && b == Private )
    return Private;

  if( a == KillPrivate && b == Private )
    return Private;

  if( a == SharePrivate && b == Private )
    return Private;

  if( a == SharePrivate && b == KillPrivate )
    return Private;

  return Unclassified;
}

// {{{ Printing
void HeapAssignment::print(raw_ostream &fout) const
{
  std::string name;

  fout << "Classification report:";
  if( success.empty() )
  {
    fout << " Not valid.\n";
    name = "(invalid)";
  }
  else
  {
    fout << " Valid for: ";
    for(LoopSet::const_iterator i=success.begin(), e=success.end(); i!=e; ++i)
    {
      const BasicBlock *header = *i;
      const Function *fcn = header->getParent();

      fout << "  Loop " << fcn->getName() << " :: " << header->getName() << ", ";
      name = ("(" + fcn->getName() + " :: " + header->getName() + ")").str();
    }
    fout << '\n';

    if( success.size() > 1 )
      name = "(combined)";
  }

  fout << "  Found " << shareds.size() << " shared AUs:\n";
  for(AUSet::const_iterator i=shareds.begin(), e=shareds.end(); i!=e; ++i)
  {
    AU *au = *i;
    fout << "    o shared";
    int sh = getSubHeap(au);
    if( -1 != sh )
      fout << "[sh=" << sh << ']';
    fout << ' ' << *au << ' ' << name << " #regression\n";
  }
  fout << "  Found " << locals.size() << " local AUs:\n";
  for(AUSet::const_iterator i=locals.begin(), e=locals.end(); i!=e; ++i)
  {
    AU *au = *i;
    fout << "    o local";
    int sh = getSubHeap(au);
    if( -1 != sh )
      fout << "[sh=" << sh << ']';
    fout << ' ' << *au << ' ' << name << " #regression\n";
  }
  fout << "  Found " << reduxs.size() << " reduction AUs:\n";
  for(ReduxAUSet::const_iterator i=reduxs.begin(), e=reduxs.end(); i!=e; ++i)
  {
    AU *au = i->first;
    Reduction::Type rt = i->second;
    fout << "    o redux("<< Reduction::names[rt] <<")";
    int sh = getSubHeap(au);
    if( -1 != sh )
      fout << "[sh=" << sh << ']';
    fout << ' ' << *au << ' ' << name << " #regression\n";
  }
  fout << "  Found " << privs.size() << " private AUs:\n";
  for(AUSet::const_iterator i=privs.begin(), e=privs.end(); i!=e; ++i)
  {
    AU *au = *i;
    fout << "    o priv";
    int sh = getSubHeap(au);
    if( -1 != sh )
      fout << "[sh=" << sh << ']';
    fout << ' ' << *au << ' ' << name << " #regression\n";
  }
  if (cheap_privs.size()) {
    fout << "  Found " << cheap_privs.size() << " cheap private AUs:\n";
    for(auto i : cheap_privs)
    {
      AU *au = i.first;
      fout << "    o cheap_priv";
      int sh = getSubHeap(au);
      if( -1 != sh )
        fout << "[sh=" << sh << ']';
      fout << ' ' << *au << ' ' << name << " #regression\n";
    }
  }
  fout << "  Found " << kill_privs.size() << " kill private AUs:\n";
  for ( AUSet::const_iterator i=kill_privs.begin(), e=kill_privs.end(); i != e; ++i )
  {
    AU *au = *i;
    fout << "    o kill_priv";
    int sh = getSubHeap(au);
    if ( sh != -1 )
      fout << "[sh=" << sh << ']';
    fout << ' ' << *au << ' ' << name << " #regression\n";
  }
  fout << "  Found " << share_privs.size() << " shareable private AUs:\n";
  for ( AUSet::const_iterator i=share_privs.begin(), e=share_privs.end(); i != e; ++i )
  {
    AU *au = *i;
    fout << "    o share_priv";
    int sh = getSubHeap(au);
    if ( sh != -1 )
      fout << "[sh=" << sh << ']';
    fout << ' ' << *au << ' ' << name << " #regression\n";
  }
  fout << "  Found " << ros.size() << " read-only (live-in) AUs:\n";
  for(AUSet::const_iterator i=ros.begin(), e=ros.end(); i!=e; ++i)
  {
    AU *au = *i;
    fout << "    o ro";
    int sh = getSubHeap(au);
    if( -1 != sh )
      fout << "[sh=" << sh << ']';
    fout << ' ' << *au << ' ' << name << " #regression\n";
  }
}

void HeapAssignment::dump() const
{
  print( errs() );
}

raw_ostream &operator<<(raw_ostream &fout, const HeapAssignment &assg)
{
  assg.print(fout);
  return fout;
}
// }}}

int HeapAssignment::getSubHeap(const AU *au) const
{
  SubheapAssignment::const_iterator i = subheaps.find(au);
  if( i == subheaps.end() )
    return -1;

  return i->second;
}

int HeapAssignment::getSubHeap(const Value *ptr, const Loop *loop, const Read &spresults) const
{
  // Map ptr to AUs.
  Ptrs aus;
  const Ctx *ctx = spresults.getCtx(loop);
  if( ! spresults.getUnderlyingAUs(ptr,ctx,aus) )
    return -1;

  return getSubHeap(aus);
}

static int joinSubHeaps(int a, int b)
{
  if( -1 == a )
    return b;
  if( -1 == b )
    return a;
  if( a == b )
    return a;
  else
    return -1;
}

int HeapAssignment::getSubHeap(Ptrs &aus) const
{
  int res = -1;
  for(unsigned i=0; i<aus.size(); ++i)
  {
    AU *au = aus[i].au;
    if( au->type == AU_Null )
      continue;

    if( au->type == AU_Unknown )
      return -1;

    if( au->type == AU_Undefined )
      return -1;

    res = joinSubHeaps(res, getSubHeap(au) );
  }

  return res;
}

void HeapAssignment::setSubHeap(const AU *au, int sh)
{
  if( 0 > sh )
    subheaps.erase(au);

  else
    subheaps[au] = sh;
}

void HeapAssignment::setValidFor(const Loop *loop)
{
  success.insert( loop->getHeader() );
}

bool HeapAssignment::isValid() const
{
  return !success.empty();
}

bool HeapAssignment::isValidFor(const Loop *L) const
{
  return success.count( L->getHeader() );
}

bool HeapAssignment::isSimpleCase() const
{
  std::set<const Value *> allocationSites;
  const AUSet &shareds    = getSharedAUs(),
              &locals     = getLocalAUs(),
              &privates   = getPrivateAUs(),
              &killprivs  = getKillPrivAUs(),
              &shareprivs = getSharePrivAUs(),
              &ro         = getReadOnlyAUs();

  const ReduxAUSet &reductions = getReductionAUs();

  for(AUSet::const_iterator i=shareds.begin(), e=shareds.end(); i!=e; ++i)
  {
    AU *au = *i;
    if (!au->value) {
      errs() << "Empty value in au : " << *au << "\n";
      continue;
    }
    allocationSites.insert( au->value );
  }
  for(AUSet::const_iterator i=locals.begin(), e=locals.end(); i!=e; ++i)
  {
    AU *au = *i;
    if( au->value && allocationSites.count( au->value ) )
      return false;
  }
  for(AUSet::const_iterator i=locals.begin(), e=locals.end(); i!=e; ++i)
  {
    AU *au = *i;
    if (!au->value) {
      errs() << "Empty value in au : " << *au << "\n";
      continue;
    }
    allocationSites.insert( au->value );
  }
  for(ReduxAUSet::const_iterator i=reductions.begin(), e=reductions.end(); i!=e; ++i)
  {
    AU *au = i->first;
    if( au->value && allocationSites.count( au->value ) )
      return false;
  }
  for(ReduxAUSet::const_iterator i=reductions.begin(), e=reductions.end(); i!=e; ++i)
  {
    AU *au = i->first;
    if (!au->value) {
      errs() << "Empty value in au : " << *au << "\n";
      continue;
    }
    allocationSites.insert( au->value );
  }
  for(AUSet::const_iterator i=privates.begin(), e=privates.end(); i!=e; ++i)
  {
    AU *au = *i;
    if( au->value && allocationSites.count( au->value ) )
      return false;
  }
  for(AUSet::const_iterator i=privates.begin(), e=privates.end(); i!=e; ++i)
  {
    AU *au = *i;
    if (!au->value) {
      errs() << "Empty value in au : " << *au << "\n";
      continue;
    }
    allocationSites.insert( au->value );
  }
  for(AUSet::const_iterator i=killprivs.begin(), e=killprivs.end(); i!=e; ++i)
  {
    AU *au = *i;
    if( au->value && allocationSites.count( au->value ) )
      return false;
  }
  for(AUSet::const_iterator i=killprivs.begin(), e=killprivs.end(); i!=e; ++i)
  {
    AU *au = *i;
    if (!au->value) {
      errs() << "Empty value in au : " << *au << "\n";
      continue;
    }
    allocationSites.insert( au->value );
  }
  for(AUSet::const_iterator i=shareprivs.begin(), e=shareprivs.end(); i!=e; ++i)
  {
    AU *au = *i;
    if( au->value && allocationSites.count( au->value ) )
      return false;
  }
  for(AUSet::const_iterator i=shareprivs.begin(), e=shareprivs.end(); i!=e; ++i)
  {
    AU *au = *i;
    if (!au->value) {
      errs() << "Empty value in au : " << *au << "\n";
      continue;
    }
    allocationSites.insert( au->value );
  }
  for(AUSet::const_iterator i=ro.begin(), e=ro.end(); i!=e; ++i)
  {
    AU *au = *i;
    if (!au->value) {
      errs() << "Empty value in au : " << *au << "\n";
    }
    if( au->value && allocationSites.count( au->value ) )
      return false;
  }
  /*
  for(AUSet::const_iterator i=ro.begin(), e=ro.end(); i!=e; ++i)
  {
    AU *au = *i;
    allocationSites.insert( au->value );
  }
  */

  return true;
}

HeapAssignment::Type HeapAssignment::classify(const Value *ptr, const Loop *loop, const Read &spresults) const
{
  // Map ptr to AUs.
  Ptrs aus;
  const Ctx *ctx = spresults.getCtx(loop);
  if( ! spresults.getUnderlyingAUs(ptr,ctx,aus) )
    return HeapAssignment::Unclassified;

  return classify(aus);
}

HeapAssignment::Type HeapAssignment::classify(Ptrs &aus) const
{
  Type res = Unclassified;
  for(unsigned i=0; i<aus.size(); ++i)
  {
    AU *au = aus[i].au;
    if( au->type == AU_Null )
      continue;

    if( au->type == AU_Unknown )
      return Unclassified;

    if( au->type == AU_Undefined )
      return Unclassified;

    Type ty = classify(au);

    // first time through loop
    if( res == Unclassified )
      res = ty;

    // later iterations: ensure consistency!
    else if( res != ty )
      return Unclassified;
  }

  return res;
}

HeapAssignment::Type HeapAssignment::classify(AU *au) const
{
  const AUSet &shareds = getSharedAUs();
  if( shareds.count(au) )
    return Shared;

  const AUSet &locals = getLocalAUs();
  if( locals.count(au) )
    return Local;

  const ReduxAUSet &reductions = getReductionAUs();
  if( reductions.count(au) )
    return Redux;

  const AUSet &privates = getPrivateAUs();
  if( privates.count(au) )
    return Private;

  const AUSet &ro = getReadOnlyAUs();
  if( ro.count(au) )
    return ReadOnly;

  const AUSet &killprivs = getKillPrivAUs();
  if( killprivs.count(au) )
    return KillPrivate;

  const AUSet &shareprivs = getSharePrivAUs();
  if( shareprivs.count(au) )
    return SharePrivate;

//  errs() << "AU not classified within loop: " << *au << '\n';
  return Unclassified;
}

bool HeapAssignment::subOfAUSet(Ptrs &aus, const AUSet &auSet) {
  for (unsigned i = 0; i < aus.size(); ++i) {
    AU *au = aus[i].au;
    if (au->type == AU_Null)
      continue;

    if (au->type == AU_Unknown)
      return false;

    if (au->type == AU_Undefined)
      return false;

    if (!auSet.count(au)) {
      return false;
    }
  }
  return true;
}

bool HeapAssignment::subOfAUSet(Ptrs &aus, const AUToRemeds &ausR) {
  if (aus.size() == 0 || ausR.empty())
    return false;

  for (unsigned i = 0; i < aus.size(); ++i) {
    AU *au = aus[i].au;
    if (au->type == AU_Null)
      continue;

    if (au->type == AU_Unknown)
      return false;

    if (au->type == AU_Undefined)
      return false;

    if (!ausR.count(au)) {
      return false;
    }
  }
  return true;
}

Remedies HeapAssignment::getRemedForPrivAUs(Ptrs &aus) const {
  Remedies allR;
  for (unsigned i = 0; i < aus.size(); ++i) {
    AU *au = aus[i].au;
    if (au->type == AU_Null)
      continue;
    if (!cheap_privs.count(au))
      continue;
    const Remedies &R = cheap_privs.at(au);
    for (auto remed : R)
      allR.insert(remed);
  }
  return allR;
}

Remedies HeapAssignment::getRemedForNoWAW(Ptrs &aus) const {
  Remedies allR;
  for (unsigned i = 0; i < aus.size(); ++i) {
    AU *au = aus[i].au;
    if (au->type == AU_Null)
      continue;
    if (!no_waw_remeds.count(au))
      continue;
    const Remedies &R = no_waw_remeds.at(au);
    for (auto remed : R)
      allR.insert(remed);
  }
  return allR;
}

HeapAssignment::AUSet &HeapAssignment::getSharedAUs() {  return shareds; }
HeapAssignment::AUSet &HeapAssignment::getLocalAUs() { return locals; }
HeapAssignment::AUSet &HeapAssignment::getPrivateAUs() { return privs; }
HeapAssignment::AUSet &HeapAssignment::getKillPrivAUs() { return kill_privs; }
HeapAssignment::AUSet &HeapAssignment::getSharePrivAUs() { return share_privs; }
HeapAssignment::AUSet &HeapAssignment::getReadOnlyAUs() { return ros; }
HeapAssignment::AUToRemeds &HeapAssignment::getCheapPrivAUs() { return cheap_privs; }
HeapAssignment::AUToRemeds &HeapAssignment::getNoWAWRemeds() { return no_waw_remeds; }
HeapAssignment::ReduxAUSet &HeapAssignment::getReductionAUs() { return reduxs; }
HeapAssignment::ReduxDepAUSet &HeapAssignment::getReduxDepAUs() { return reduxdeps; }
HeapAssignment::ReduxRegAUSet &HeapAssignment::getReduxRegAUs() { return reduxregs; }

const HeapAssignment::AUSet &HeapAssignment::getSharedAUs() const { return shareds; }
const HeapAssignment::AUSet &HeapAssignment::getLocalAUs() const { return locals; }
const HeapAssignment::AUSet &HeapAssignment::getPrivateAUs() const { return privs; }
const HeapAssignment::AUSet &HeapAssignment::getKillPrivAUs() const { return kill_privs; }
const HeapAssignment::AUSet &HeapAssignment::getSharePrivAUs() const { return share_privs; }
const HeapAssignment::AUSet &HeapAssignment::getReadOnlyAUs() const { return ros; }
const HeapAssignment::AUToRemeds &HeapAssignment::getCheapPrivAUs() const { return cheap_privs; }
const HeapAssignment::AUToRemeds &HeapAssignment::getNoWAWRemeds() const { return no_waw_remeds; }
const HeapAssignment::ReduxAUSet &HeapAssignment::getReductionAUs() const { return reduxs; }
const HeapAssignment::ReduxDepAUSet &HeapAssignment::getReduxDepAUs() const { return reduxdeps; }
const HeapAssignment::ReduxRegAUSet &HeapAssignment::getReduxRegAUs() const { return reduxregs; }

bool HeapAssignment::compatibleWith(const HeapAssignment &other) const
{
  // For each of my AUs:
  return compatibleWith(ReadOnly, other.getReadOnlyAUs())
  &&     compatibleWith(Shared,   other.getSharedAUs())
  &&     compatibleWith(Redux,    other.getReductionAUs())
  &&     compatibleWith(Local,    other.getLocalAUs())
  &&     compatibleWith(Private,  other.getPrivateAUs())
  &&     compatibleWith(KillPrivate,  other.getKillPrivAUs())
  &&     compatibleWith(SharePrivate, other.getSharePrivAUs());
}

bool HeapAssignment::compatibleWith(Type ty, const AUSet &set) const
{
  assert( ty != Redux );

  for(AUSet::const_iterator i=set.begin(), e=set.end(); i!=e; ++i)
  {
    Type myty = classify(*i);
    if( myty == Unclassified )
      continue;

    if( join(myty, ty) == Unclassified )
      return false;
  }
  return true;
}

bool HeapAssignment::compatibleWith(Type ty, const ReduxAUSet &rset) const
{
  assert( ty == Redux );

  for(ReduxAUSet::const_iterator i=rset.begin(), e=rset.end(); i!=e; ++i)
  {
    Type myty = classify(i->first);
    if( myty == Unclassified )
      continue;

    if( join(myty,ty) == Unclassified )
      return false;

    if( myty == Redux )
    {
      const ReduxAUSet &myrx = getReductionAUs();
      ReduxAUSet::const_iterator j = myrx.find( i->first );
      assert( j != myrx.end() );

      // Ensure they are the same kind of reduction
      if( i->second != j->second )
        return false;
    }
  }
  return true;
}

void HeapAssignment::accumulate(const HeapAssignment &A, Type ty0, const AUSet &Bset)
{
  assert( ty0 != Redux );
  for(AUSet::const_iterator i=Bset.begin(), e=Bset.end(); i!=e; ++i)
  {
    AU *au = *i;
    Type ty = A.classify(au);
    if( ty == Unclassified )
      ty = ty0;
    else
      ty = join(ty, ty0);
    assert( ty != Unclassified );

    switch( ty )
    {
      case ReadOnly:
        ros.insert(au);
        break;
      case Shared:
        shareds.insert(au);
        break;
      case Redux:
        assert( false && "Impossible?!");
        break;
      case Local:
        locals.insert(au);
        break;
      case Private:
        privs.insert(au);
        break;
      case KillPrivate:
        kill_privs.insert(au);
        break;
      case SharePrivate:
        share_privs.insert(au);
        break;
      default:
        assert( false && "This should not happen");
        break;
    }
  }
}

void HeapAssignment::accumulate(const HeapAssignment &A, Type ty0, const ReduxAUSet &Bset)
{
  assert( ty0 == Redux );
  for(ReduxAUSet::const_iterator i=Bset.begin(), e=Bset.end(); i!=e; ++i)
  {
    AU *au = i->first;
    Type ty = A.classify(au);
    if( ty == Unclassified )
      ty = ty0;
    else
      ty = join(ty, ty0);
    assert( ty != Unclassified );

    assert( ty == Redux );
    ReduxAUSet::const_iterator j = A.reduxs.find(au);
    if( j != A.reduxs.end() )
      assert( j->second == i->second );
    reduxs[ au ] = i->second;
  }
}

HeapAssignment HeapAssignment::operator&(const HeapAssignment &other) const
{
  HeapAssignment asgn;

  asgn.accumulate(*this, ReadOnly, other.getReadOnlyAUs());
  asgn.accumulate(*this, Shared, other.getSharedAUs());
  asgn.accumulate(*this, Redux, other.getReductionAUs());
  asgn.accumulate(*this, Local, other.getLocalAUs());
  asgn.accumulate(*this, Private, other.getPrivateAUs());
  asgn.accumulate(*this, KillPrivate, other.getKillPrivAUs());
  asgn.accumulate(*this, SharePrivate, other.getSharePrivAUs());

  asgn.accumulate(other, ReadOnly, this->getReadOnlyAUs());
  asgn.accumulate(other, Shared, this->getSharedAUs());
  asgn.accumulate(other, Redux, this->getReductionAUs());
  asgn.accumulate(other, Local, this->getLocalAUs());
  asgn.accumulate(other, Private, this->getPrivateAUs());
  asgn.accumulate(other, KillPrivate, this->getKillPrivAUs());
  asgn.accumulate(other, SharePrivate, this->getSharePrivAUs());

  asgn.success.insert( this->success.begin(), this->success.end() );
  asgn.success.insert( other.success.begin(), other.success.end() );

  asgn.assignSubHeaps();
  return asgn;
}

void HeapAssignment::contextRenamedViaClone(
  const Ctx *changedContext,
  const ValueToValueMapTy &vmap,
  const CtxToCtxMap &cmap,
  const AuToAuMap &amap)
{
//  errs() << "  . . - HeapAssignment::contextRenamedViaClone: " << *changedContext << '\n';

  const ValueToValueMapTy::const_iterator vmap_end = vmap.end();

  LoopSet newLoops;
  for(LoopSet::const_iterator i=success.begin(), e=success.end(); i!=e; ++i)
  {
    const BasicBlock *header = *i;
    newLoops.insert(header);
    ValueToValueMapTy::const_iterator j = vmap.find(header);
    if( j == vmap_end )
      continue;
    const BasicBlock *newHeader = cast< BasicBlock >( &*(j->second) );
    newLoops.insert( newHeader );
  }
  success.insert( newLoops.begin(), newLoops.end() );

  updateAUSet(shareds, amap);
  updateAUSet(locals, amap);
  updateAUSet(privs, amap);
  updateAUSet(kill_privs, amap);
  updateAUSet(share_privs, amap);
  updateAUSet(ros, amap);
  updateAUSet(reduxs, amap);
}

void HeapAssignment::updateAUSet(AUSet &aus, const AuToAuMap &amap)
{
  const AuToAuMap::const_iterator amap_end = amap.end();

  AUSet newSet;
  for(AUSet::const_iterator i=aus.begin(), e=aus.end(); i!=e; ++i)
  {
    AU *old = *i;
    AuToAuMap::const_iterator j = amap.find( old );
    if( j == amap_end )
      newSet.insert( old );
    else
      newSet.insert( j->second );
  }

  aus.swap(newSet);
}

void HeapAssignment::updateAUSet(ReduxAUSet &aus, const AuToAuMap &amap)
{
  const AuToAuMap::const_iterator amap_end = amap.end();

  ReduxAUSet newSet;
  for(ReduxAUSet::const_iterator i=aus.begin(), e=aus.end(); i!=e; ++i)
  {
    AU *old = i->first;
    Reduction::Type ty = i->second;

    AuToAuMap::const_iterator j = amap.find( old );
    if( j == amap_end )
      newSet.insert( ReduxAUSet::value_type(old,ty) );
    else
      newSet.insert( ReduxAUSet::value_type(j->second, ty) );
  }

  aus.swap(newSet);
}

void Classify::contextRenamedViaClone(
  const Ctx *cc,
  const ValueToValueMapTy &vmap,
  const CtxToCtxMap &cmap,
  const AuToAuMap &amap)
{
//  errs() << "  . . - Classify::contextRenamedViaClone: " << *cc << '\n';
  const ValueToValueMapTy::const_iterator vend = vmap.end();

  Loop2Assignments new_asgns;
  for(Loop2Assignments::iterator i=assignments.begin(), e=assignments.end(); i!=e; ++i)
  {
    const BasicBlock *header = i->first;
    i->second.contextRenamedViaClone(cc,vmap,cmap,amap);

    const ValueToValueMapTy::const_iterator j = vmap.find(header);
    if( j != vend )
    {
      const BasicBlock *new_header = cast< BasicBlock >( &*( j->second ) );
      new_asgns[ new_header ] = i->second;
    }
  }

  assignments.insert( new_asgns.begin(), new_asgns.end() );
}

bool compatible(const HeapAssignment &A, const HeapAssignment &B)
{
  return A.compatibleWith(B) && B.compatibleWith(A);
}

}
}
