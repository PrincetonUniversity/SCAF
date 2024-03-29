#ifndef LLVM_LIBERTY_LLVM_AA_RESULTS_H
#define LLVM_LIBERTY_LLVM_AA_RESULTS_H

#include "llvm/Analysis/AliasAnalysis.h"
#include "llvm/Analysis/BasicAliasAnalysis.h"
#include "llvm/IR/DataLayout.h"
#include "llvm/IR/Dominators.h"
#include "llvm/IR/Module.h"
#include "llvm/Pass.h"

#include "scaf/MemoryAnalysisModules/LoopAA.h"

namespace liberty {
using namespace arcana::noelle;
class LLVMAAResults : public ModulePass, public LoopAA {
  const DataLayout *DL;

  LegacyAARGetter *AARGetter;
  AAResults *aa;
  Function *curF;

public:
  static char ID;
  LLVMAAResults();
  ~LLVMAAResults();

  bool runOnModule(Module &M) {
    DL = &M.getDataLayout();
    InitializeLoopAA(this, *DL);
    AARGetter = new LegacyAARGetter(*this);
    return false;
  }

  StringRef getLoopAAName() const { return "llvm-aa-results"; }

  virtual SchedulingPreference getSchedulingPreference() const {
    return SchedulingPreference(Normal - 5);
  }

  void getAnalysisUsage(AnalysisUsage &AU) const {
    LoopAA::getAnalysisUsage(AU);
    // AU.addRequired<AAResultsWrapperPass>();
    AU.addRequired<AssumptionCacheTracker>();
    AU.addRequired<DominatorTreeWrapperPass>();
    getAAResultsAnalysisUsage(AU);
    AU.setPreservesAll();
  }

  void computeAAResults(const Function *cf);

  /// getAdjustedAnalysisPointer - This method is used when a pass implements
  /// an analysis interface through multiple inheritance.  If needed, it
  /// should override this to adjust the this pointer as needed for the
  /// specified pass info.
  virtual void *getAdjustedAnalysisPointer(AnalysisID PI) {
    if (PI == &LoopAA::ID)
      return (LoopAA *)this;
    return this;
  }

  AliasResult alias(const Value *ptrA, unsigned sizeA, TemporalRelation rel,
                    const Value *ptrB, unsigned sizeB, const Loop *L,
                    Remedies &R, DesiredAliasResult dAliasRes = DNoOrMustAlias);

  ModRefResult modref(const Instruction *i1, TemporalRelation Rel,
                      const Value *p2, unsigned sz2, const Loop *L,
                      Remedies &R);

  ModRefResult modref(const Instruction *i1, TemporalRelation Rel,
                      const Instruction *i2, const Loop *L, Remedies &R);
};
} // namespace liberty

#endif
