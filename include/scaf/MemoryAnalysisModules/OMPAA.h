#ifndef LLVM_LIBERTY_OMP_AA_H
#define LLVM_LIBERTY_OMP_AA_H

#include "llvm/Analysis/AliasAnalysis.h"
#include "llvm/Analysis/BasicAliasAnalysis.h"
#include "llvm/IR/DataLayout.h"
#include "llvm/IR/Module.h"
#include "llvm/Pass.h"

#include "scaf/MemoryAnalysisModules/LoopAA.h"
#include "scaf/Utilities/ModuleLoops.h"


namespace liberty {
using namespace llvm::noelle;
class OMPAA : public ModulePass, public LoopAA {

  const DataLayout *DL;
  ModuleLoops *mloops;

public:
  static char ID;
  OMPAA();
  ~OMPAA();

  void setModuleLoops(ModuleLoops *ml) { mloops = ml; }

  bool runOnModule(Module &M) {
    DL = &M.getDataLayout();
    InitializeLoopAA(this, *DL);
    setModuleLoops(&getAnalysis<ModuleLoops>());
    return false;
  }

  void getAnalysisUsage(AnalysisUsage &AU) const {
    LoopAA::getAnalysisUsage(AU);
    AU.addRequired<ModuleLoops>();
    AU.setPreservesAll();
  }

  virtual void *getAdjustedAnalysisPointer(AnalysisID PI) {
    if (PI == &LoopAA::ID)
      return (LoopAA *)this;
    return this;
  }

  StringRef getLoopAAName() const { return "omp-aa"; }

  virtual SchedulingPreference getSchedulingPreference() const {
    return SchedulingPreference(Normal);
  }

  std::unordered_set<std::string> parseAnnotationsForInst(const Instruction *I) {
    std::unordered_set<std::string> pragmas;
    MDNode *metaData = I->getMetadata("note.noelle");
    if(!metaData)
      return pragmas;

    for(auto i = 0; i < metaData->getNumOperands(); i++)
      pragmas.insert(cast<MDString>(dyn_cast<MDNode>(metaData->getOperand(i))->getOperand(0))->getString());

    return pragmas;
  }

  AliasResult alias(const Value *ptrA, unsigned sizeA, TemporalRelation rel,
                    const Value *ptrB, unsigned sizeB, const Loop *L,
                    Remedies &R, DesiredAliasResult dAliasRes = DNoOrMustAlias);

  ModRefResult modref(const Instruction *A, TemporalRelation rel,
                      const Value *ptrB, unsigned sizeB, const Loop *L,
                      Remedies &R);

  ModRefResult modref(const Instruction *A, TemporalRelation rel,
                      const Instruction *B, const Loop *L, Remedies &R);
};

} // namespace liberty
#endif
