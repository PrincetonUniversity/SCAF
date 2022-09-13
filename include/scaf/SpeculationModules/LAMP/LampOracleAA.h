#ifndef LLVM_LIBERTY_LAMP_ORACLE_AA_H
#define LLVM_LIBERTY_LAMP_ORACLE_AA_H

#include "scaf/MemoryAnalysisModules/LoopAA.h"
#include "scaf/SpeculationModules/Remediator.h"
#include "LAMPLoadProfile.h"

namespace liberty
{
using namespace llvm;
using namespace llvm::noelle;

class LampRemedy : public Remedy {
  public:
    const Instruction *srcI;
    const Instruction *dstI;

    bool compare(const Remedy_ptr rhs) const override;
    void setCost(PerformanceEstimator *perf);
    StringRef getRemedyName() const override { return "lamp-remedy"; };

    bool isExpensive() override { return true; }
};

class LampOracle : public LoopAA, public Remediator // Not a pass!
{
    LAMPLoadProfile *lamp;
    PerformanceEstimator *perf;

  public:
    LampOracle(LAMPLoadProfile *l) : LoopAA(), lamp(l) {perf = nullptr;}

    StringRef getLoopAAName() const { return "lamp-oracle-aa"; }
    StringRef getRemediatorName() const override { return "lamp-oracle-remed"; }

    AliasResult alias(const Value *ptrA, unsigned sizeA, TemporalRelation rel,
                      const Value *ptrB, unsigned sizeB, const Loop *L,
                      Remedies &R,
                      DesiredAliasResult dAliasRes = DNoOrMustAlias);

    ModRefResult modref(
      const Instruction *A,
      TemporalRelation rel,
      const Value *ptrB, unsigned sizeB,
      const Loop *L, Remedies &R);

    ModRefResult modref(
      const Instruction *A,
      TemporalRelation rel,
      const Instruction *B,
      const Loop *L,
      Remedies &R);

    RemedResp memdep(const Instruction *A, const Instruction *B, bool loopCarried, DataDepType dataDepTy, const Loop *L) override;
};

}

#endif

