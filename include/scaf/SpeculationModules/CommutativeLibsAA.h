#ifndef LLVM_LIBERTY_COMM_LIBS_AA_H
#define LLVM_LIBERTY_COMM_LIBS_AA_H

#include "scaf/MemoryAnalysisModules/LoopAA.h"
#include "scaf/SpeculationModules/Remediator.h"

namespace liberty {
using namespace llvm;
using namespace arcana::noelle;

class CommutativeLibsRemedy : public Remedy {
public:
  StringRef functionName;

  // void apply(Task *task);
  bool compare(const Remedy_ptr rhs) const;
  StringRef getRemedyName() const { return "comm-libs-remedy"; };
};

struct CommutativeLibsAA : public LoopAA, Remediator // Not a pass!
{
  CommutativeLibsAA() : LoopAA() {}

  StringRef getLoopAAName() const { return "comm-libs-aa"; }
  StringRef getRemediatorName() const override { return "comm-libs-remed"; }

  LoopAA::ModRefResult modref(const Instruction *A, TemporalRelation rel,
                              const Value *ptrB, unsigned sizeB, const Loop *L,
                              Remedies &R);

  LoopAA::ModRefResult modref(const Instruction *A, TemporalRelation rel,
                              const Instruction *B, const Loop *L, Remedies &R);

  LoopAA::SchedulingPreference getSchedulingPreference() const {
    return SchedulingPreference(Low - 10);
  }

  RemedResp memdep(const Instruction *A, const Instruction *B, bool loopCarried, DataDepType dataDepTy, const Loop *L) override;

private:
  // functions that are usually considered commutative
  static const std::unordered_set<std::string> CommFunNamesSet;

  Function *getCalledFun(const Instruction *A);
};
} // namespace liberty

#endif

