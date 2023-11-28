#ifndef LLVM_LIBERTY_MEM_SPEC_AA_REMED_H
#define LLVM_LIBERTY_MEM_SPEC_AA_REMED_H

#include "llvm/IR/DataLayout.h"

#include "scaf/MemoryAnalysisModules/KillFlow.h"
#include "scaf/MemoryAnalysisModules/LoopAA.h"
#include "scaf/MemoryAnalysisModules/QueryCacheing.h"
#include "scaf/MemoryAnalysisModules/SimpleAA.h"
#include "scaf/SpeculationModules/LAMP/LAMPLoadProfile.h"
#include "scaf/SpeculationModules/LAMP/LampOracleAA.h"
#include "scaf/SpeculationModules/CommutativeLibsAA.h"
#include "scaf/SpeculationModules/EdgeCountOracleAA.h"
#include "scaf/SpeculationModules/LocalityAA.h"
#include "scaf/SpeculationModules/PointsToAA.h"
#include "scaf/SpeculationModules/PrivAA.h"
#include "scaf/SpeculationModules/PtrResidueAA.h"
#include "scaf/SpeculationModules/ReadOnlyAA.h"
#include "scaf/SpeculationModules/Remediator.h"
#include "scaf/SpeculationModules/ShortLivedAA.h"
#include "scaf/SpeculationModules/SmtxAA.h"
#include "scaf/SpeculationModules/TXIOAA.h"
#include "scaf/SpeculationModules/CallsiteDepthCombinator_CtrlSpecAware.h"
#include "scaf/SpeculationModules/Classify.h"
#include "scaf/SpeculationModules/ControlSpeculator.h"
#include "scaf/SpeculationModules/KillFlow_CtrlSpecAware.h"
#include "scaf/SpeculationModules/PredictionSpeculator.h"
#include "scaf/SpeculationModules/Read.h"

namespace liberty {
using namespace llvm;
using namespace arcana::noelle;

class MemSpecAARemedy : public Remedy {
public:
  const Instruction *srcI;
  const Instruction *dstI;

  Remedies subR;

  void apply(Task *task);
  bool compare(const Remedy_ptr rhs) const;
  StringRef getRemedyName() const { return "mem-spec-aa-remedy"; };

  bool hasSubRemedies() { return true; }
  Remedies *getSubRemedies() { return &subR; }
};

class MemSpecAARemediator : public Remediator {
public:
  MemSpecAARemediator(Pass &p, ControlSpeculation *cs, LAMPLoadProfile *lp,
                      const Read &read, const HeapAssignment &c,
                      PredictionSpeculation *ps,
                      SpecPriv::SmtxSpeculationManager *sman,
                      PtrResidueSpeculationManager *pman,
                      KillFlow_CtrlSpecAware *killflowA,
                      CallsiteDepthCombinator_CtrlSpecAware *callsiteA,
                      ModuleLoops &ml, PerformanceEstimator *pf)
      : Remediator(), proxy(p), ctrlspec(cs), lamp(lp), spresults(read),
        asgn(c), predspec(ps), smtxMan(sman), ptrresMan(pman),
        killflow_aware(killflowA), callsite_aware(callsiteA),
        mloops(ml), perf(pf) {
          killFlow = proxy.getAnalysisIfAvailable<KillFlow>();
        }

  StringRef getRemediatorName() const { return "mem-spec-aa-remediator"; }

  Remedies satisfy(const PDG &pdg, Loop *loop, const Criticisms &criticisms);

  RemedResp memdep(const Instruction *A, const Instruction *B, bool LoopCarried,
                   DataDepType dataDepTy, const Loop *L);

private:
  Pass &proxy;
  ControlSpeculation *ctrlspec;
  EdgeCountOracle *edgeaa;
  LAMPLoadProfile *lamp;
  // LampOracle *lampaa;
  SmtxAA *smtxaa;
  const Read &spresults;
  PointsToAA *pointstoaa;
  const HeapAssignment &asgn;
  LocalityAA *localityaa;
  PredictionSpeculation *predspec;
  SpecPriv::SmtxSpeculationManager *smtxMan;
  PredictionAA *predaa;
  PtrResidueAA *ptrresaa;
  PtrResidueSpeculationManager *ptrresMan;
  ReadOnlyAA *roaa;
  ShortLivedAA *localaa;
  TXIOAA *txioaa;
  CommutativeLibsAA *commlibsaa;
  SimpleAA *simpleaa;
  KillFlow_CtrlSpecAware *killflow_aware;
  CallsiteDepthCombinator_CtrlSpecAware *callsite_aware;
  KillFlow *killFlow;
  ModuleLoops &mloops;
  PrivAA *privaa;
  PerformanceEstimator *perf;
};

} // namespace liberty

#endif
