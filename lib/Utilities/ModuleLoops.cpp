#include "llvm/IR/DataLayout.h"
#include "llvm/IR/Function.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/IR/LegacyPassManagers.h"

#include "scaf/Utilities/ModuleLoops.h"

#define DEBUG_TYPE "moduleloops"

namespace liberty {
using namespace llvm;

GimmeLoops &ModuleLoops::compute(const Function *fcn) {
  if (!results.count(fcn)) {
    // Evil, but okay because NONE of these passes modify the IR
    Function *non_const_function = const_cast<Function *>(fcn);

    //Fix needed due to llvm commit bf22593
    //AssumptionCache now independently checks for TargetTransformInfoWrapperPass, requires PMTopLevelManager
    auto pmt = this->getResolver()->getPMDataManager().getTopLevelManager();

    results[fcn] = new GimmeLoops(pmt);
    results[fcn]->init(td, non_const_function, true);
  }

  return *results[fcn];
}

DominatorTree &ModuleLoops::getAnalysis_DominatorTree(const Function *fcn) {
  GimmeLoops &gl = compute(fcn);
  return *gl.getDT();
}

PostDominatorTree &
ModuleLoops::getAnalysis_PostDominatorTree(const Function *fcn) {
  GimmeLoops &gl = compute(fcn);
  return *gl.getPDT();
}

LoopInfo &ModuleLoops::getAnalysis_LoopInfo(const Function *fcn) {
  GimmeLoops &gl = compute(fcn);
  return *gl.getLI();
}

ScalarEvolution &ModuleLoops::getAnalysis_ScalarEvolution(const Function *fcn) {
  GimmeLoops &gl = compute(fcn);
  return *gl.getSE();
}

char ModuleLoops::ID = 0;
static RegisterPass<ModuleLoops> rp("mloops",
                                    "ModuleLoops: get your pass manager on...");

} // namespace liberty
