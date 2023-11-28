#ifndef LLVM_LIBERTY_DSMTX_TXIOAA_H
#define LLVM_LIBERTY_DSMTX_TXIOAA_H

#include "scaf/MemoryAnalysisModules/LoopAA.h"
#include "scaf/SpeculationModules/Remediator.h"

namespace liberty {
using namespace llvm;
using namespace arcana::noelle;

class TXIORemedy : public Remedy {
public:
  const Instruction *printI;

  void apply(Task *task);
  bool compare(const Remedy_ptr rhs) const;
  StringRef getRemedyName() const { return "txio-remedy"; };
};

struct TXIOAA : public LoopAA, Remediator // Not a pass!
{
  TXIOAA() : LoopAA() {}
  ~TXIOAA() = default;

  StringRef getLoopAAName() const override { return "txio-aa"; }
  StringRef getRemediatorName() const override { return "txio-remed"; }

  AliasResult alias(const Value *ptrA, unsigned sizeA, TemporalRelation rel,
                    const Value *ptrB, unsigned sizeB, const Loop *L,
                    Remedies &R, DesiredAliasResult dAliasRes = DNoOrMustAlias) override;

  ModRefResult modref(const Instruction *A, TemporalRelation rel,
                      const Value *ptrB, unsigned sizeB, const Loop *L,
                      Remedies &R) override;

  ModRefResult modref(const Instruction *A, TemporalRelation rel,
                      const Instruction *B, const Loop *L, Remedies &R) override;

  static bool isTXIOFcn(const Instruction *inst);

  LoopAA::SchedulingPreference getSchedulingPreference() const override {
    return SchedulingPreference(Low - 9);
  }

  RemedResp memdep(const Instruction *A, const Instruction *B, bool loopCarried, DataDepType dataDepTy, const Loop *L) override;
};

} // namespace liberty

#endif

