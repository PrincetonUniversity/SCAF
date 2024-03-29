#define DEBUG_TYPE "mem-spec-aa-remed"

#include "scaf/SpeculationModules/MemSpecAARemed.h"

#include "llvm/ADT/Statistic.h"
#include "llvm/IR/IntrinsicInst.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/raw_ostream.h"

#define DEFAULT_MEM_SPEC_AA_REMED_COST 1500

namespace liberty {

using namespace llvm;
using namespace arcana::noelle;

STATISTIC(numQueries, "Num queries");
STATISTIC(numNoFlow, "Num no-flow results");

void MemSpecAARemedy::apply(Task *task) {
  // TODO: transfer the code for application of mem-spec-aa here.
}

bool MemSpecAARemedy::compare(const Remedy_ptr rhs) const {
  std::shared_ptr<MemSpecAARemedy> memSpecAARhs =
      std::static_pointer_cast<MemSpecAARemedy>(rhs);
  if (this->srcI == memSpecAARhs->srcI)
    return this->dstI < memSpecAARhs->dstI;
  return this->srcI < memSpecAARhs->srcI;
}

Remedies MemSpecAARemediator::satisfy(const PDG &pdg, Loop *loop,
                                      const Criticisms &criticisms) {

  const DataLayout &DL = loop->getHeader()->getModule()->getDataLayout();

  // CtrlSpec
  edgeaa = new EdgeCountOracle(ctrlspec);
  edgeaa->InitializeLoopAA(&proxy, DL);

  // LAMP
  //lampaa = new LampOracle(lamp);
  //lampaa->InitializeLoopAA(&proxy, DL);

  smtxaa = new SmtxAA(smtxMan, perf);
  smtxaa->InitializeLoopAA(&proxy, DL);

  // Points-to
  // cannot validate points-to
  pointstoaa = new PointsToAA(spresults);
  pointstoaa->InitializeLoopAA(&proxy, DL);

  // Separation Spec
  const Ctx *ctx = spresults.getCtx(loop);
  localityaa = new LocalityAA(spresults, asgn, ctx, perf);
  localityaa->InitializeLoopAA(&proxy, DL);

  // Value prediction
  predaa = new PredictionAA(predspec, perf);
  predaa->InitializeLoopAA(&proxy, DL);

  // Ptr-residue
  ptrresaa = new PtrResidueAA(DL, *ptrresMan, perf);
  ptrresaa->InitializeLoopAA(&proxy, DL);

  // read-only aa
  roaa = new ReadOnlyAA(spresults, asgn, ctx, perf);
  roaa->InitializeLoopAA(&proxy, DL);

  // short-lived aa
  localaa = new ShortLivedAA(spresults, asgn, ctx, perf);
  localaa->InitializeLoopAA(&proxy, DL);

  txioaa = new TXIOAA();
  txioaa->InitializeLoopAA(&proxy, DL);

  commlibsaa = new CommutativeLibsAA();
  commlibsaa->InitializeLoopAA(&proxy, DL);

  simpleaa = new SimpleAA();
  simpleaa->InitializeLoopAA(&proxy, DL);

  // cheap priv and full-overwrite
  if (killFlow) {
    privaa = new PrivAA(spresults, asgn, ctx, *killFlow, mloops, loop);
    privaa->InitializeLoopAA(&proxy, DL);
  }
  

  ctrlspec->setLoopOfInterest(loop->getHeader());
  predaa->setLoopOfInterest(loop);

  if (killflow_aware) {
    killflow_aware->setLoopOfInterest(ctrlspec, loop);
    killflow_aware->InitializeLoopAA(&proxy, DL);
  }

  if (callsite_aware) {
    callsite_aware->setLoopOfInterest(ctrlspec, loop);
    callsite_aware->InitializeLoopAA(&proxy, DL);
  }

  predaa->getTopAA()->dump();

  Remedies remedies = Remediator::satisfy(pdg, loop, criticisms);

  // remove these AAs from the stack by destroying them
  delete edgeaa;
  //delete lampaa;

  delete smtxaa;
  delete pointstoaa;
  delete localityaa;
  delete predaa;
  delete ptrresaa;
  delete roaa;
  delete localaa;
  delete txioaa;
  delete commlibsaa;
  delete simpleaa;
  delete privaa;

  if (killflow_aware){
    killflow_aware->setLoopOfInterest(nullptr, nullptr);
  }

  //killflow_aware->getTopAA()->dump();

  return remedies;
}

Remediator::RemedResp MemSpecAARemediator::memdep(const Instruction *A,
                                                  const Instruction *B,
                                                  bool LoopCarried,
                                                  DataDepType dataDepTy,
                                                  const Loop *L) {
  ++numQueries;
  Remediator::RemedResp remedResp;
  // conservative answer
  remedResp.depRes = DepResult::Dep;
  std::shared_ptr<MemSpecAARemedy> remedy =
      std::shared_ptr<MemSpecAARemedy>(new MemSpecAARemedy());
  remedy->cost = DEFAULT_MEM_SPEC_AA_REMED_COST;
  remedy->srcI = A;
  remedy->dstI = B;

  // avoid intra-iter memory speculation. Presence of speculated II complicates
  // validation process, and potentially forces high false positive rate of
  // misspecs or extensive and regular checkpoints. Benefits for parallelization
  // have not proven to be significant in preliminary experiments. If further
  // experiments prove otherwise this change might be reverted. Note that
  // neither Hanjun nor Taewook used II mem spec for similar reasons.
  if (!LoopCarried) {
    remedResp.remedy = remedy;
    return remedResp;
  }

  // This AA stack includes static analysis, flow dependence speculation,
  // locality, value prediction and control speculation.
  LoopAA *aa = predaa->getTopAA();
  //aa->dump();
  Remedies R;

  bool RAW = dataDepTy == DataDepType::RAW;
  bool WAW = dataDepTy == DataDepType::WAW;
  bool noDep =
      noMemoryDep(A, B, LoopAA::Before, LoopAA::After, L, aa, RAW, WAW, R);
  if (noDep) {
    ++numNoFlow;
    remedy->subR = R;
    remedResp.depRes = DepResult::NoDep;
  }

  remedResp.remedy = remedy;
  return remedResp;
}

} // namespace liberty
