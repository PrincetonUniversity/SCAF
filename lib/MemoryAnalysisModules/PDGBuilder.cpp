#include "llvm/Analysis/AssumptionCache.h"
#include "llvm/IR/InstrTypes.h"
#include <sstream>
#include <streambuf>

#include "llvm/Analysis/LoopInfo.h"
#include "llvm/Analysis/PostDominators.h"
#include "llvm/Analysis/ScalarEvolution.h"
#include "llvm/Analysis/ScalarEvolutionExpressions.h"
#include "llvm/Analysis/ValueTracking.h"
#include "llvm/Analysis/TargetTransformInfo.h"
#include "llvm/IR/BasicBlock.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/LegacyPassManager.h"
#include "llvm/Pass.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/Transforms/IPO/PassManagerBuilder.h"

#include "llvm/ADT/iterator_range.h"

//#include "scaf/Utilities/ReportDump.h"
//#include "scaf/Utilities/Metadata.h"

//#include "noelle/core/PDGPrinter.hpp"
#include "scaf/Utilities/ModuleLoops.h"
#include "scaf/MemoryAnalysisModules/GetMemoryDependences.h"
#include "Assumptions.h"

#define DEBUG_TYPE "pdgbuilder"

enum MemDepType {
  MEM_RAW = 0x4,
  MEM_WAR = 0x2,
  MEM_WAW = 0x1,
};

using namespace llvm;
using namespace llvm::noelle;
using namespace liberty;

static cl::opt<bool> DumpPDG(
    "dump-pdg", cl::init(false), cl::NotHidden,
    cl::desc("Dump PDG"));

void llvm::PDGBuilder::getAnalysisUsage(AnalysisUsage &AU) const {
  //AU.addRequired<TargetLibraryInfoWrapperPass>();
  AU.addRequired<LoopInfoWrapperPass>();
  AU.addRequired< LoopAA >();
  //AU.addRequired<PostDominatorTreeWrapperPass>();
  AU.addRequired<ScalarEvolutionWrapperPass>();
  AU.addRequired<TargetTransformInfoWrapperPass>();
  AU.addRequired<AssumptionCacheTracker>();

  AU.addRequired< ModuleLoops >();

  AU.setPreservesAll();
}

bool llvm::PDGBuilder::runOnModule(Module &M){
  DL = &M.getDataLayout();

  ModuleLoops &mloops = getAnalysis< ModuleLoops >();
  LoopAA *aa = getAnalysis< LoopAA >().getTopAA();
  aa->dump();

  for(Module::iterator i = M.begin(), e = M.end(); i != e; i++) {
    Function &F = *i;
    if(F.isDeclaration())
      continue;
    LoopInfo &li = mloops.getAnalysis_LoopInfo(&F);
    //LoopInfo &li = getAnalysis<LoopInfoWrapperPass>(F).getLoopInfo();
    std::vector<Loop*> loops(li.begin(), li.end());
    while(!loops.empty()) {
      Loop *l = loops.back();
      loops.pop_back();
      auto pdg = getLoopPDG(l, aa);
    
      loops.insert(loops.end(), l->getSubLoops().begin(), l->getSubLoops().end());
      if(DumpPDG) {
        std::string filename;
        raw_string_ostream ros(filename);
        ros << "pdg-function-" << F.getName() << "-loop-" << this->loopCount++ << ".dot";
        //llvm::noelle::DGPrinter::writeClusteredGraph<PDG, Value>(ros.str(), pdg.get());
      }
    }
    
    
    //std::vector<Loop*> loops(li->begin(), li->end());
    //while(!loops.empty()) {
    //  Loop *l = loops.back();
    //  errs() << "Loop: " << l->getHeader()->getName() << "\n";
    //  loops.pop_back();
    //  auto pdg = getLoopPDG(l, aa);
    //
    //  loops.insert(loops.end(), l->getSubLoops().begin(), l->getSubLoops().end());
    //}
  }
  return false;
}

std::unique_ptr<llvm::noelle::PDG> llvm::PDGBuilder::getLoopPDG(Loop *loop, LoopAA *aa) {
  auto pdg = std::make_unique<llvm::noelle::PDG>(loop);

  LLVM_DEBUG(errs() << "constructEdgesFromMemory with CAF ...\n");
  constructEdgesFromMemory(*pdg, loop, aa);

  return pdg;
}

void llvm::PDGBuilder::constructEdgesFromMemory(PDG &pdg, Loop *loop,
                                                 LoopAA *aa) {
  errs() << "Starting " << loop->getHeader()->getParent()->getName() << ":" << loop->getName() << "\n";
  noctrlspec.setLoopOfInterest(loop->getHeader());
  unsigned long memDepQueryCnt = 0;
  unsigned long lcMemDepCnt = 0;
  unsigned long iiMemDepCnt = 0;
  for (auto nodeI : make_range(pdg.begin_nodes(), pdg.end_nodes())) {
    Value *pdgValueI = nodeI->getT();
    Instruction *i = dyn_cast<Instruction>(pdgValueI);
    assert(i && "Expecting an instruction as the value of a PDG node");

    if (!i->mayReadOrWriteMemory())
      continue;

    for (auto nodeJ : make_range(pdg.begin_nodes(), pdg.end_nodes())) {
      Value *pdgValueJ = nodeJ->getT();
      Instruction *j = dyn_cast<Instruction>(pdgValueJ);
      assert(j && "Expecting an instruction as the value of a PDG node");

      if (!j->mayReadOrWriteMemory())
        continue;

      ++memDepQueryCnt;

      unsigned lc = queryLoopCarriedMemoryDep(i, j, loop, aa, pdg);
      unsigned ii = queryIntraIterationMemoryDep(i, j, loop, aa, pdg);
      lcMemDepCnt += lc >> 2;
      iiMemDepCnt += ii >> 2;
    }
  }
  errs() << "Total queries: " << memDepQueryCnt << "\n";
  errs() << "Total LC RAW deps: " << lcMemDepCnt << "\n";
  errs() << "Total II RAW deps: " << iiMemDepCnt << "\n";
}

// query memory dep conservatively (with only memory analysis modules in the
// stack)
uint8_t llvm::PDGBuilder::queryMemoryDep(Instruction *src, Instruction *dst,
                                      LoopAA::TemporalRelation FW,
                                      LoopAA::TemporalRelation RV, Loop *loop,
                                      LoopAA *aa, PDG &pdg) {
  if (!src->mayReadOrWriteMemory())
    return 0;
  if (!dst->mayReadOrWriteMemory())
    return 0;
  if (!src->mayWriteToMemory() && !dst->mayWriteToMemory())
    return 0;

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
    return 0;

  // reverse dep test
  LoopAA::ModRefResult reverse = forward;

  if (loopCarried || src != dst)
    reverse = aa->modref(dst, RV, src, loop, R);

  if (!dst->mayWriteToMemory())
    reverse = LoopAA::ModRefResult(reverse & (~LoopAA::Mod));
  if (!dst->mayReadFromMemory())
    reverse = LoopAA::ModRefResult(reverse & (~LoopAA::Ref));

  if (LoopAA::NoModRef == reverse)
    return 0;

  if (LoopAA::Ref == forward && LoopAA::Ref == reverse)
    return 0; // RaR dep; who cares.

  // At this point, we know there is one or more of
  // a flow-, anti-, or output-dependence.
  uint8_t res = 0;

  bool RAW = (forward == LoopAA::Mod || forward == LoopAA::ModRef) &&
             (reverse == LoopAA::Ref || reverse == LoopAA::ModRef);
  bool WAR = (forward == LoopAA::Ref || forward == LoopAA::ModRef) &&
             (reverse == LoopAA::Mod || reverse == LoopAA::ModRef);
  bool WAW = (forward == LoopAA::Mod || forward == LoopAA::ModRef) &&
             (reverse == LoopAA::Mod || reverse == LoopAA::ModRef);

  if (RAW) {
    auto edge = pdg.addEdge((Value *)src, (Value *)dst);
    edge->setMemMustType(true, false, DG_RAW);
    edge->setLoopCarried(loopCarried);
    res |= MEM_RAW;
    if(loop->getName() == "for.cond" && loop->getHeader()->getParent()->getName() == "kernel_2mm") {
      errs () << *src << " -> " << *dst << "\n";
    }
  }
  if (WAR) {
    auto edge = pdg.addEdge((Value *)src, (Value *)dst);
    edge->setMemMustType(true, false, DG_WAR);
    edge->setLoopCarried(loopCarried);
    res |= MEM_WAR;
  }
  if (WAW) {
    auto edge = pdg.addEdge((Value *)src, (Value *)dst);
    edge->setMemMustType(true, false, DG_WAW);
    edge->setLoopCarried(loopCarried);
    res |= MEM_WAW;
  }
  return res;
}

uint8_t llvm::PDGBuilder::queryIntraIterationMemoryDep(Instruction *src,
                                                     Instruction *dst,
                                                     Loop *loop, LoopAA *aa,
                                                     PDG &pdg) {
  
  if(noctrlspec.isReachable(src, dst, loop))
    return queryMemoryDep(src, dst, LoopAA::Same, LoopAA::Same, loop, aa, pdg);
  return 0;
}

uint8_t llvm::PDGBuilder::queryLoopCarriedMemoryDep(Instruction *src,
                                                 Instruction *dst, Loop *loop,
                                                 LoopAA *aa, PDG &pdg) {
  // there is always a feasible path for inter-iteration deps
  // (there is a path from any node in the loop to the header
  //  and the header dominates all the nodes of the loops)

  // only need to check for aliasing and kill-flow

  return queryMemoryDep(src, dst, LoopAA::Before, LoopAA::After, loop, aa, pdg);
}

char PDGBuilder::ID = 0;
static RegisterPass< PDGBuilder > X("pdgbuilder", "PDGBuilder", false, true);