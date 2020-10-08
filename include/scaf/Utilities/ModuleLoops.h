#ifndef LLVM_LIBERTY_MODULE_LOOPS_H
#define LLVM_LIBERTY_MODULE_LOOPS_H

#include "llvm/Analysis/LoopInfo.h"
#include "llvm/Analysis/PostDominators.h"
#include "llvm/Analysis/ScalarEvolution.h"
#include "llvm/Analysis/TargetLibraryInfo.h"
#include "llvm/IR/DataLayout.h"
#include "llvm/IR/Dominators.h"
#include "llvm/IR/Module.h"
#include "llvm/Pass.h"

#include "scaf/Utilities/GimmeLoops.h"

#include <map>

namespace llvm {
class DominatorTree;
class PostDominatorTree;
class LoopInfo;
class ScalarEvolution;
} // namespace llvm

namespace liberty {
using namespace llvm;

struct ModuleLoops : public ModulePass {
  static char ID;
  ModuleLoops() : ModulePass(ID) {}
  ~ModuleLoops() { reset(); }

  void getAnalysisUsage(AnalysisUsage &au) const {
    au.addRequired<TargetLibraryInfoWrapperPass>();
    au.setPreservesAll();
  }

  bool runOnModule(Module &mod) {
    td = &mod.getDataLayout();
    TargetLibraryInfoWrapperPass *tliWrap =
        &getAnalysis<TargetLibraryInfoWrapperPass>();
    tli = &tliWrap->getTLI();
    return false;
  }

  void reset() {
    for (auto r : results)
      delete r;
    results.clear();
  }

  void forget(Function *fcn) {
    if (results.count(fcn)) {
      GimmeLoops *gl = results[fcn];
      results.erase(fcn);
      delete gl;
    }
  }

  DominatorTree &getAnalysis_DominatorTree(const Function *fcn);
  PostDominatorTree &getAnalysis_PostDominatorTree(const Function *fcn);
  LoopInfo &getAnalysis_LoopInfo(const Function *fcn);
  ScalarEvolution &getAnalysis_ScalarEvolution(const Function *fcn);

private:
  const DataLayout *td;
  TargetLibraryInfo *tli;
  std::map<const Function *, GimmeLoops *> results;

  GimmeLoops &compute(const Function *fcn);
};

} // namespace liberty

#endif

