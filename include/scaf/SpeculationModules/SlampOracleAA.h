#ifndef LLVM_LIBERTY_SLAMP_SLAMP_ORACLE_AA_H
#define LLVM_LIBERTY_SLAMP_SLAMP_ORACLE_AA_H

#include "scaf/MemoryAnalysisModules/LoopAA.h"
#include "scaf/SpeculationModules/Remediator.h"
#include "scaf/SpeculationModules/SLAMPLoad.h"

namespace liberty
{

using namespace llvm;
using namespace slamp;

class SlampRemedy : public Remedy {
public:
  const Instruction *srcI;
  const Instruction *dstI;

  // void apply(Task *task);
  bool compare(const Remedy_ptr rhs) const override;
  void setCost(PerformanceEstimator *perf);
  StringRef getRemedyName() const override { return "slamp-remedy"; };

  bool isExpensive() override { return true; }
};

struct SlampOracleAA : public LoopAA, public Remediator
{
public:
  //SlampOracle(SLAMPLoadProfile *l) : slamp(l) {}
  SlampOracleAA(SLAMPLoadProfile *l) : LoopAA(), slamp(l) {perf = nullptr;}
  //~SlampOracle() {}

  StringRef getLoopAAName() const { return "slamp-oracle-aa"; }
  StringRef getRemediatorName() const override { return "slamp-oracle-remed"; }

  AliasResult alias(const Value *ptrA, unsigned sizeA, TemporalRelation rel,
                    const Value *ptrB, unsigned sizeB, const Loop *L,
                    Remedies &R, DesiredAliasResult dAliasRes = DNoOrMustAlias);

  ModRefResult modref(
    const Instruction *A,
    TemporalRelation rel,
    const Value *ptrB, unsigned sizeB,
    const Loop *L, Remedies &R);

  ModRefResult modref(
    const Instruction *A,
    TemporalRelation rel,
    const Instruction *B,
    const Loop *L, Remedies &R);

  RemedResp memdep(const Instruction *A, const Instruction *B, bool loopCarried, DataDepType dataDepTy, const Loop *L) override;

private:
  SLAMPLoadProfile *slamp;
  PerformanceEstimator *perf;
};

}

#endif
