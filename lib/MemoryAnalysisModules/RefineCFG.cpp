#define DEBUG_TYPE "refine-cfg"

#include "llvm/IR/InstIterator.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/raw_ostream.h"

#include "scaf/Utilities/CallBaseFactory.h"

#include "RefineCFG.h"

using namespace llvm;

bool RefineCFG::runOnModule(Module &M) {

  bool changed = false;

  CG = &getAnalysis<CallGraphWrapperPass>().getCallGraph();

  typedef Module::const_iterator ModuleIt;
  for (ModuleIt fun = M.begin(); fun != M.end(); ++fun) {
    if (!fun->isDeclaration())
      changed |= runOnFunction(*fun);
  }

  return changed;
}

bool RefineCFG::runOnFunction(const Function &F) {
  bool changed = false;

  for (auto inst = inst_begin(F); inst != inst_end(F); ++inst) {
    changed |= runOnCallBase(liberty::getCallBase(&*inst));
  }

  return changed;
}

bool RefineCFG::runOnCallBase(const CallBase *CS) {

  if (!CS)
    return false;

  const Value *target = CS->getCalledOperand();
  const Function *targetFun = dyn_cast<Function>(target->stripPointerCasts());
  if (!targetFun)
    return false;

  if (target == targetFun)
    return false;

  const Function *F = CS->getParent()->getParent();

  // FIXME: very bad, bypass const pointer
  auto CS_n = (CallBase *)CS;
  (*CG)[F]->addCalledFunction(CS_n, (*CG)[targetFun]);
  LLVM_DEBUG(errs() << "RefineCFG: " << F->getName() << " calls "
                    << targetFun->getName() << "\n");

  return false;
}

void RefineCFG::getAnalysisUsage(AnalysisUsage &AU) const {
  AU.addRequired<CallGraphWrapperPass>();
  AU.setPreservesAll();
}

char RefineCFG::ID = 0;

static RegisterPass<RefineCFG> X("refine-cfg", "Disambiguate edges in the CFG",
                                 false, true);
