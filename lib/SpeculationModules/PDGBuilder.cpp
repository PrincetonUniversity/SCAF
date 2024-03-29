#include "llvm/IR/InstrTypes.h"
#include <sstream>
#include <streambuf>
#define DEBUG_TYPE "pdgbuilder"

#include "llvm/Analysis/LoopInfo.h"
#include "llvm/Analysis/PostDominators.h"
#include "llvm/Analysis/ScalarEvolution.h"
#include "llvm/Analysis/ScalarEvolutionExpressions.h"
#include "llvm/Analysis/ValueTracking.h"
#include "llvm/IR/BasicBlock.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/LegacyPassManager.h"
#include "llvm/Pass.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/Transforms/IPO/PassManagerBuilder.h"

#include "llvm/ADT/iterator_range.h"

#include "scaf/SpeculationModules/GlobalConfig.h"
#include "scaf/MemoryAnalysisModules/LLVMAAResults.h"
#include "scaf/SpeculationModules/PDGBuilder.hpp"
#include "scaf/SpeculationModules/ProfilePerformanceEstimator.h"
#include "scaf/Utilities/ReportDump.h"
#include "scaf/SpeculationModules/LoopProf/Targets.h"
#include "scaf/Utilities/Metadata.h"
#include "scaf/SpeculationModules/SLAMPLoad.h"
#include "scaf/SpeculationModules/SlampOracleAA.h"

#include "noelle/core/PDGPrinter.hpp"
#include "Assumptions.h"

using namespace llvm;
using namespace arcana::noelle;
using namespace liberty;

static cl::opt<bool> DumpPDG(
    "dump-pdg",
    cl::init(false),
    cl::NotHidden,
    cl::desc("Dump out the PDG as dot files"));

static cl::opt<std::string> QueryDep(
  "query-dep", cl::init(""), cl::NotHidden,
  cl::desc("Query a specific dependence"));

cl::opt<bool> EnableEdgeProf = cl::opt<bool> ( "enable-edgeprof",
    cl::init(false),
    cl::NotHidden,
    cl::desc("Enable edge prof and control spec modules"));

cl::opt<bool> EnableLamp = cl::opt<bool> ( "enable-lamp",
    cl::init(false),
    cl::NotHidden,
    cl::desc("Enable LAMP and mem spec modules"));

cl::opt<bool> EnableSlamp = cl::opt<bool> ( "enable-slamp",
    cl::init(false),
    cl::NotHidden,
    cl::desc("Enable SLAMP and mem spec modules"));

cl::opt<bool> EnableSpecPriv = cl::opt<bool> ( "enable-specpriv",
    cl::init(false),
    cl::NotHidden,
    cl::desc("Enable SpecPriv and related modules"));

cl::opt<bool> IgnoreCallsite = cl::opt<bool> ( "pdg-ignore-callsite",
    cl::init(false),
    cl::NotHidden,
    cl::desc("Ignore all callsite in PDG"));

void llvm::PDGBuilder::getAnalysisUsage(AnalysisUsage &AU) const {
  AU.addRequired<TargetLibraryInfoWrapperPass>();
  AU.addRequired<LoopInfoWrapperPass>();
  AU.addRequired< LoopAA >();
  AU.addRequired<PostDominatorTreeWrapperPass>();
  // AU.addRequired<LLVMAAResults>();

  if (EnableEdgeProf) {
    AU.addRequired<ProfileGuidedControlSpeculator>();
    //AU.addRequired<KillFlow_CtrlSpecAware>();
    //AU.addRequired<CallsiteDepthCombinator_CtrlSpecAware>();
  }

  if (EnableLamp) {
    AU.addRequired<SmtxSpeculationManager>();
  }

  if (EnableSlamp) {
    AU.addRequired<SLAMPLoadProfile>();
    AU.addRequired<SlampOracleAA>();
  }

  if (EnableSpecPriv) {
    AU.addRequired<ProfileGuidedPredictionSpeculator>();
    AU.addRequired<PtrResidueSpeculationManager>();
    AU.addRequired<ReadPass>();
    AU.addRequired<Classify>();
  }

  AU.addRequired< ProfilePerformanceEstimator >();
  AU.addRequired< Targets >();
  AU.addRequired< ModuleLoops >();

  AU.setPreservesAll();
}

bool llvm::PDGBuilder::runOnModule (Module &M){
  DL = &M.getDataLayout();
  if (DumpPDG) {
    // LoopProf is always required
    ModuleLoops &mloops = getAnalysis< ModuleLoops >();
    Targets &targets = getAnalysis< Targets >();
    for(Targets::iterator i=targets.begin(mloops), e=targets.end(mloops); i!=e; ++i) {
      Loop *loop = *i;
      auto pdg = getLoopPDG(loop);

      // dump pdg to dot files
      std::string filename;
      raw_string_ostream ros(filename);
      ros << "pdg-function-" << loop->getHeader()->getParent()->getName() << "-loop" << this->loopCount++ << "-refined.dot";
      arcana::noelle::DGPrinter::writeClusteredGraph<PDG, Value>(ros.str(), pdg.get());
    }
  }

  std::stringstream ss(QueryDep);
  int instrIdSrc, instrIdDst;

  auto findInstr = [] (Loop *loop, int instrId) {
    for (auto &block : loop->getBlocks()) {
      for (auto &instr : *block) {
        if (liberty::Namer::getInstrId(&instr) == instrId)
          return &instr;
      }
    }
    return (Instruction *)nullptr;
  };

  // if there's content in both field
  if (ss >> instrIdSrc >> instrIdDst) { 
    ModuleLoops &mloops = getAnalysis< ModuleLoops >();
    Targets &targets = getAnalysis< Targets >();
    for(Targets::iterator i=targets.begin(mloops), e=targets.end(mloops); i!=e; ++i) {
      Loop *loop = *i;
      Instruction *src = findInstr(loop, instrIdSrc);
      Instruction *dst = findInstr(loop, instrIdDst);

      errs() << "In loop " << loop->getHeader()->getParent()->getName()  << "::" << loop->getHeader()->getName() << "\n";
      // get deps
      //errs() << "Control Dep: ";
      //errs() << "Control Dep (LC): ";
      errs() << "Deps between instr \n" << *src << "\n -> \n" << *dst << "\n";

      auto llvmaa = getAnalysisIfAvailable<LLVMAAResults>();
      if (llvmaa) {
        llvmaa->computeAAResults(loop->getHeader()->getParent());
      }
      LoopAA *aa = getAnalysis< LoopAA >().getTopAA();

      auto pdg = std::make_unique<arcana::noelle::PDG>(loop);

      queryLoopCarriedMemoryDep(src, dst, loop, aa, *pdg);
      queryIntraIterationMemoryDep(src, dst, loop, aa, *pdg);

      for (auto edge: pdg->fetchEdges(pdg->fetchNode(src), pdg->fetchNode(dst))) {
        errs() << edge->toString() << "\n";
      }
      errs() << "End of deps";
      //errs() << "Reg Dep: ";
      //errs() << "Reg Dep (LC): ";
    }

  }

  return false;
}

std::unique_ptr<arcana::noelle::PDG> llvm::PDGBuilder::getLoopPDG(Loop *loop) {
  auto pdg = std::make_unique<arcana::noelle::PDG>(loop);

  REPORT_DUMP(errs() << "constructEdgesFromMemory with CAF ...\n");
  auto llvmaa = getAnalysisIfAvailable<LLVMAAResults>();
  if (llvmaa) {
    llvmaa->computeAAResults(loop->getHeader()->getParent());
  }
  LoopAA *aa = getAnalysis< LoopAA >().getTopAA();
  aa->dump();
  constructEdgesFromMemory(*pdg, loop, aa);

  REPORT_DUMP(errs() << "annotateMemDepsWithRemedies with SCAF ...\n");
  annotateMemDepsWithRemedies(*pdg,loop,aa);

  REPORT_DUMP(errs() << "construct Edges From Control ...\n");

  constructEdgesFromControl(*pdg, loop);

  REPORT_DUMP(errs() << "construct Edges From UseDefs ...\n");

  // constructEdgesFromUseDefs adds external nodes for live-ins and live-outs
  constructEdgesFromUseDefs(*pdg, loop);

  REPORT_DUMP(errs() << "PDG construction completed\n");

  return pdg;
}

void llvm::PDGBuilder::addSpecModulesToLoopAA() {
  PerformanceEstimator *perf = &getAnalysis<ProfilePerformanceEstimator>();

  if (EnableLamp) {
    auto &smtxMan = getAnalysis<SmtxSpeculationManager>();
    smtxaa = new SmtxAA(&smtxMan, perf); // LAMP
    smtxaa->InitializeLoopAA(this, *DL);
  }

  if (EnableSlamp) {
    auto &slamp = getAnalysis<SLAMPLoadProfile>();
    slampaa = new SlampOracleAA(&slamp);
    slampaa->InitializeLoopAA(this, *DL);
  }

  if (EnableEdgeProf) {
    ctrlspec = getAnalysis<ProfileGuidedControlSpeculator>().getControlSpecPtr();
    edgeaa = new EdgeCountOracle(ctrlspec); // Control Spec
    edgeaa->InitializeLoopAA(this, *DL);
    //killflow_aware = &getAnalysis<KillFlow_CtrlSpecAware>(); // KillFlow
    //callsite_aware = &getAnalysis<CallsiteDepthCombinator_CtrlSpecAware>(); // CallsiteDepth
  }

  if (EnableSpecPriv) {
    predspec =
      getAnalysis<ProfileGuidedPredictionSpeculator>().getPredictionSpecPtr();
    predaa = new PredictionAA(predspec, perf); //Value Prediction 
    predaa->InitializeLoopAA(this, *DL);

    PtrResidueSpeculationManager &ptrresMan =
      getAnalysis<PtrResidueSpeculationManager>();
    ptrresaa = new PtrResidueAA(*DL, ptrresMan, perf); // Pointer Residue SpecPriv
    ptrresaa->InitializeLoopAA(this, *DL);

    spresults = &getAnalysis<ReadPass>().getProfileInfo(); // SpecPriv Results
    classify = &getAnalysis<Classify>(); // SpecPriv Classify

    // cannot validate points-to object info.
    // should only be used within localityAA validation only for points-to heap
    // use it to explore coverage. points-to is always avoided
    pointstoaa = new PointsToAA(*spresults);
    pointstoaa->InitializeLoopAA(this, *DL);
  }

  // FIXME: try to add txio and commlib back to PDG Building
  txioaa = new TXIOAA();
  txioaa->InitializeLoopAA(this, *DL);

  commlibsaa = new CommutativeLibsAA();
  commlibsaa->InitializeLoopAA(this, *DL);

  simpleaa = new SimpleAA();
  simpleaa->InitializeLoopAA(this, *DL);
}

void llvm::PDGBuilder::specModulesLoopSetup(Loop *loop) {
  PerformanceEstimator *perf = &getAnalysis<ProfilePerformanceEstimator>();

  if (EnableEdgeProf) {
    ctrlspec->setLoopOfInterest(loop->getHeader());
    //killflow_aware->setLoopOfInterest(ctrlspec, loop);
    //callsite_aware->setLoopOfInterest(ctrlspec, loop);
  }

  if (EnableSpecPriv) {
    predaa->setLoopOfInterest(loop);

    const HeapAssignment &asgn = classify->getAssignmentFor(loop);
    if (!asgn.isValidFor(loop)) {
      errs() << "ASSIGNMENT INVALID FOR LOOP: "
        << loop->getHeader()->getParent()->getName()
        << "::" << loop->getHeader()->getName() << '\n';
    }

    const Ctx *ctx = spresults->getCtx(loop);
    roaa = new ReadOnlyAA(*spresults, asgn, ctx, perf);
    roaa->InitializeLoopAA(this, *DL);

    localaa = new ShortLivedAA(*spresults, asgn, ctx, perf);
    localaa->InitializeLoopAA(this, *DL);
  }
}

void llvm::PDGBuilder::removeSpecModulesFromLoopAA() {
  // c++ guarantee that if null nothing bad will happen
  delete slampaa;
  delete smtxaa;
  delete edgeaa;
  delete predaa;
  delete ptrresaa;
  delete pointstoaa;
  delete roaa;
  delete localaa;
  delete txioaa;
  delete commlibsaa;
  delete simpleaa;
  if (killflow_aware) {
    killflow_aware->setLoopOfInterest(nullptr, nullptr);
  }
}

void llvm::PDGBuilder::constructEdgesFromUseDefs(PDG &pdg, Loop *loop) {
  for (auto inodePair : pdg.internalNodePairs()) {
    Value *pdgValue = inodePair.first;
    if (pdgValue->getNumUses() == 0)
      continue;

    for (auto &U : pdgValue->uses()) {
      auto user = U.getUser();

      // is argument possible here?
      if (isa<Instruction>(user) || isa<Argument>(user)) {
        const PHINode *phi = dyn_cast<PHINode>(user);
        bool loopCarried = (phi && phi->getParent() == loop->getHeader());

        // add external node if not already there. Used for live-outs
        if (!pdg.isInternal(user))
          pdg.fetchOrAddNode(user, /*internal=*/ false);

        auto edge = pdg.addEdge(pdgValue, user);
        edge->setMemMustType(false, true, DG_DATA_RAW);
        edge->setLoopCarried(loopCarried);
      }
    }

    // add register dependences and external nodes for live-ins
    Instruction *user = dyn_cast<Instruction>(pdgValue);
    assert(user && "A node of loop pdg is not an Instruction");

    // For each user's (loop inst) operand which is not loop instruction,
    // add reg dep (deps among loop insts are already added)
    for (User::op_iterator j = user->op_begin(), z = user->op_end(); j != z;
         ++j) {
      Value *operand = *j;

      if (!pdg.isInternal(operand) &&
          (isa<Instruction>(operand) || isa<Argument>(operand))) {
        pdg.fetchOrAddNode(operand, /*internal=*/false);

        auto edge = pdg.addEdge(operand, user);
        edge->setMemMustType(false, true, DG_DATA_RAW);
      }
    }
  }
}

void buildTransitiveIntraIterationControlDependenceCache(
    Loop *loop, PDG &pdg, PDG &IICtrlPDG,
    std::unordered_map<const Instruction *,
                       std::unordered_set<const Instruction *>>
        cache) {
  std::list<Instruction *> fringe;

  // initialization phase (populate IICtrlPDG with II ctrl deps from pdg)

  for (Loop::block_iterator j = loop->block_begin(), z = loop->block_end();
       j != z; ++j)
    for (BasicBlock::iterator k = (*j)->begin(), g = (*j)->end(); k != g; ++k) {
      Instruction *inst = &*k;
      fringe.push_back(inst);

      auto s = pdg.fetchNode(inst);
      for (auto edge : s->getOutgoingEdges()) {
        if (!edge->isControlDependence() || edge->isLoopCarriedDependence())
          continue;
        Instruction *si =
            dyn_cast<Instruction>(edge->getIncomingT());
        assert(si);
        cache[inst].insert(si);
        IICtrlPDG.addEdge((Value *)inst, (Value *)si);
      }
    }

  // update cache iteratively

  while (!fringe.empty()) {
    Instruction *v = fringe.front();
    fringe.pop_front();

    std::vector<Instruction *> updates;

    auto vN = IICtrlPDG.fetchNode(v);
    for (auto edge : vN->getOutgoingEdges()) {
      Instruction *m = dyn_cast<Instruction>(edge->getIncomingT());
      assert(m);
      auto mN = IICtrlPDG.fetchNode(m);
      for (auto edgeM : mN->getOutgoingEdges()) {
        Instruction *k =
            dyn_cast<Instruction>(edgeM->getIncomingT());
        assert(k);
        if (!cache.count(v) || !cache[v].count(k))
          updates.push_back(k);
      }
    }

    if (!updates.empty()) {
      for (unsigned i = 0 ; i < updates.size() ; i++) {
        cache[v].insert(updates[i]);
        IICtrlPDG.addEdge((Value *)v, (Value *)updates[i]);
      }
      fringe.push_back(v);
    }
  }
}

void llvm::PDGBuilder::constructEdgesFromControl(
    PDG &pdg, Loop *loop) {

  noctrlspec.setLoopOfInterest(loop->getHeader());
  SpecPriv::LoopPostDom pdt(noctrlspec, loop);

  for(Loop::block_iterator i=loop->block_begin(), e=loop->block_end(); i!=e; ++i)
  {
    ControlSpeculation::LoopBlock dst = ControlSpeculation::LoopBlock( *i );
    for(SpecPriv::LoopPostDom::pdf_iterator j=pdt.pdf_begin(dst), z=pdt.pdf_end(dst); j!=z; ++j)
    {
      ControlSpeculation::LoopBlock src = *j;

      Instruction *term = src.getBlock()->getTerminator();

      for(BasicBlock::iterator k=dst.getBlock()->begin(), f=dst.getBlock()->end(); k!=f; ++k)
      {
        Instruction *idst = &*k;

        // Draw ctrl deps to:
        //  (1) Operations with side-effects
        //  (2) Conditional branches.
        if (isSafeToSpeculativelyExecute(idst))
          continue;

        auto edge = pdg.addEdge((Value *)term, (Value *)idst);
        edge->setControl(true);
        edge->setLoopCarried(false);
      }
    }
  }

  // TODO: ideally, a PHI dependence is drawn from
  // a conditional branch to a PHI node iff the branch
  // controls which incoming value is selected by that PHI.

  // That's a pain to compute.  Instead, we will draw a
  // dependence from branches to PHIs in successors.
  for(Loop::block_iterator i=loop->block_begin(), e=loop->block_end(); i!=e; ++i)
  {
    BasicBlock *bb = *i;
    Instruction *term = bb->getTerminator();

    // no control dependence can be formulated around unconditional branches

    if (noctrlspec.isSpeculativelyUnconditional(term))
      continue;

    for(liberty::BBSuccIterator j=noctrlspec.succ_begin(bb), z=noctrlspec.succ_end(bb); j!=z; ++j)
    {
      BasicBlock *succ = *j;
      if( !loop->contains(succ) )
        continue;

      const bool loop_carried = (succ == loop->getHeader());

      for(BasicBlock::iterator k=succ->begin(); k!=succ->end(); ++k)
      {
        PHINode *phi = dyn_cast<PHINode>(&*k);
        if( !phi )
          break;
        if( phi->getNumIncomingValues() == 1 )
          continue;

        auto edge = pdg.addEdge((Value *)term, (Value *)phi);
        edge->setControl(true);
        edge->setLoopCarried(loop_carried);
      }
    }
  }

  // Add loop-carried control dependences.
  // Foreach loop-exit.
  typedef ControlSpeculation::ExitingBlocks Exitings;

  /*
  // build a tmp pdg that holds transitive II-ctrl dependence info
  std::unordered_map<const Instruction *,
                     std::unordered_set<const Instruction *>>
      IICtrlCache;
  PDG IICtrlPDG(loop);
  buildTransitiveIntraIterationControlDependenceCache(loop, pdg, IICtrlPDG, IICtrlCache);
  */

  Exitings exitings;
  noctrlspec.getExitingBlocks(loop, exitings);
  for(Exitings::iterator i=exitings.begin(), e=exitings.end(); i!=e; ++i)
  {
    BasicBlock *exiting = *i;
    Instruction *term = exiting->getTerminator();

    // Draw ctrl deps to:
    //  (1) Operations with side-effects
    //  (2) Loop exits.
    for(Loop::block_iterator j=loop->block_begin(), z=loop->block_end(); j!=z; ++j)
    {
      BasicBlock *dst = *j;
      for(BasicBlock::iterator k=dst->begin(), g=dst->end(); k!=g; ++k)
      {
        Instruction *idst = &*k;

        // Draw ctrl deps to:
        //  (1) Operations with side-effects
        //  (2) Loop exits
        if (isSafeToSpeculativelyExecute(idst))
          continue;

        /*
        if( idst->isTerminator() )
          if( ! ctrlspec.mayExit(tt,loop) )
            continue;
        */

        // Draw LC ctrl dep only when there is no (transitive) II ctrl dep from t to s

        /*
        // TODO: double-check if this is actually useful. Be more conservative
        for now to speedupthe PDG's control dep construction
        if (IICtrlCache.count(term)) if (IICtrlCache[term].count(idst)) continue;
        */

        // errs() << "new LC ctrl dep between " << *term << " and " << *idst <<
        // "\n";
        auto edge = pdg.addEdge((Value *)term, (Value *)idst);
        edge->setControl(true);
        edge->setLoopCarried(true);
      }
    }
  }
}

void llvm::PDGBuilder::constructEdgesFromMemory(PDG &pdg, Loop *loop,
                                                 LoopAA *aa) {
  noctrlspec.setLoopOfInterest(loop->getHeader());
  unsigned long memDepQueryCnt = 0;
  for (auto nodeI : make_range(pdg.begin_nodes(), pdg.end_nodes())) {
    Value *pdgValueI = nodeI->getT();
    Instruction *i = dyn_cast<Instruction>(pdgValueI);
    assert(i && "Expecting an instruction as the value of a PDG node");

    if (!i->mayReadOrWriteMemory())
      continue;

    if (IgnoreCallsite) {
      // FIXME: ignore callsite
      if (dyn_cast<CallBase>(i))
        continue;
    }

    for (auto nodeJ : make_range(pdg.begin_nodes(), pdg.end_nodes())) {
      Value *pdgValueJ = nodeJ->getT();
      Instruction *j = dyn_cast<Instruction>(pdgValueJ);
      assert(j && "Expecting an instruction as the value of a PDG node");

      if (!j->mayReadOrWriteMemory())
        continue;

      if (IgnoreCallsite) {
        // FIXME: ignore callsite
        if (dyn_cast<CallBase>(j))
          continue;
      }

      ++memDepQueryCnt;

      queryLoopCarriedMemoryDep(i, j, loop, aa, pdg);
      queryIntraIterationMemoryDep(i, j, loop, aa, pdg);
    }
  }
  REPORT_DUMP(errs() << "Total memory dependence queries to CAF: " << memDepQueryCnt
               << "\n");
}

// query memory dep conservatively (with only memory analysis modules in the
// stack)
void llvm::PDGBuilder::queryMemoryDep(Instruction *src, Instruction *dst,
                                      LoopAA::TemporalRelation FW,
                                      LoopAA::TemporalRelation RV, Loop *loop,
                                      LoopAA *aa, PDG &pdg) {
  if (!src->mayReadOrWriteMemory())
    return;
  if (!dst->mayReadOrWriteMemory())
    return;
  if (!src->mayWriteToMemory() && !dst->mayWriteToMemory())
    return;

  bool loopCarried = FW != RV;

  // No remedies used in the initial conservative PDG construction.
  // only memory analysis modules in the stack
  Remedies R;

  // forward dep test
  LoopAA::ModRefResult forward = aa->modref(src, FW, dst, loop, R);

  if (!src->mayWriteToMemory())
    forward = LoopAA::ModRefResult(forward & (~LoopAA::Mod));
  if (!src->mayReadFromMemory())
    forward = LoopAA::ModRefResult(forward & (~LoopAA::Ref));

  if (LoopAA::NoModRef == forward)
    return;

  // reverse dep test
  LoopAA::ModRefResult reverse = forward;

  if (loopCarried || src != dst)
    reverse = aa->modref(dst, RV, src, loop, R);

  if (!dst->mayWriteToMemory())
    reverse = LoopAA::ModRefResult(reverse & (~LoopAA::Mod));
  if (!dst->mayReadFromMemory())
    reverse = LoopAA::ModRefResult(reverse & (~LoopAA::Ref));

  if (LoopAA::NoModRef == reverse)
    return;

  if (LoopAA::Ref == forward && LoopAA::Ref == reverse)
    return; // RaR dep; who cares.

  // At this point, we know there is one or more of
  // a flow-, anti-, or output-dependence.

  bool RAW = (forward == LoopAA::Mod || forward == LoopAA::ModRef) &&
             (reverse == LoopAA::Ref || reverse == LoopAA::ModRef);
  bool WAR = (forward == LoopAA::Ref || forward == LoopAA::ModRef) &&
             (reverse == LoopAA::Mod || reverse == LoopAA::ModRef);
  bool WAW = (forward == LoopAA::Mod || forward == LoopAA::ModRef) &&
             (reverse == LoopAA::Mod || reverse == LoopAA::ModRef);

  if (RAW) {
    auto edge = pdg.addEdge((Value *)src, (Value *)dst);
    edge->setMemMustType(true, false, DG_DATA_RAW);
    edge->setLoopCarried(loopCarried);
  }
  if (WAR) {
    auto edge = pdg.addEdge((Value *)src, (Value *)dst);
    edge->setMemMustType(true, false, DG_DATA_WAR);
    edge->setLoopCarried(loopCarried);
  }
  if (WAW) {
    auto edge = pdg.addEdge((Value *)src, (Value *)dst);
    edge->setMemMustType(true, false, DG_DATA_WAW);
    edge->setLoopCarried(loopCarried);
  }
}

void llvm::PDGBuilder::queryIntraIterationMemoryDep(Instruction *src,
                                                     Instruction *dst,
                                                     Loop *loop, LoopAA *aa,
                                                     PDG &pdg) {
  if (noctrlspec.isReachable(src, dst, loop))
    queryMemoryDep(src, dst, LoopAA::Same, LoopAA::Same, loop, aa, pdg);
}

void llvm::PDGBuilder::queryLoopCarriedMemoryDep(Instruction *src,
                                                 Instruction *dst, Loop *loop,
                                                 LoopAA *aa, PDG &pdg) {
  // there is always a feasible path for inter-iteration deps
  // (there is a path from any node in the loop to the header
  //  and the header dominates all the nodes of the loops)

  // only need to check for aliasing and kill-flow

  queryMemoryDep(src, dst, LoopAA::Before, LoopAA::After, loop, aa, pdg);
}

void llvm::PDGBuilder::annotateMemDepsWithRemedies(PDG &pdg, Loop *loop,
                                                   LoopAA *aa) {
  // setup SCAF (add spec modules to stack)
  addSpecModulesToLoopAA();
  specModulesLoopSetup(loop);
  aa->dump();

  // try to annotate as removable every edge in the PDG with SCAF
  for (auto edge : make_range(pdg.begin_edges(), pdg.end_edges())) {

    if (!pdg.isInternal(edge->getIncomingT()) ||
        !pdg.isInternal(edge->getOutgoingT()))
      continue;

    Instruction *src = dyn_cast<Instruction>(edge->getOutgoingT());
    Instruction *dst = dyn_cast<Instruction>(edge->getIncomingT());
    assert(src && dst && "src/dst not instructions in the PDG?");

    Remedies_ptr R = std::make_shared<Remedies>();
    bool rawDep = edge->isRAWDependence();
    bool wawDep = edge->isWAWDependence();

    LoopAA::TemporalRelation FW = LoopAA::Same;
    LoopAA::TemporalRelation RV = LoopAA::Same;
    if (edge->isLoopCarriedDependence()) {
      FW = LoopAA::Before;
      RV = LoopAA::After;
    }

    bool removableEdge =
        Remediator::noMemoryDep(src, dst, FW, RV, loop, aa, rawDep, wawDep, *R);

    // annotate edge if removable
    if (removableEdge) {
      edge->addRemedies(R);
      edge->setRemovable(true);
    }
  }

  // LLVM_DEBUG(errs() << "revert stack to CAF ...\n");
  removeSpecModulesFromLoopAA();
}

char PDGBuilder::ID = 0;
static RegisterPass< PDGBuilder > rp("pdgbuilder", "PDGBuilder", false, true);
