#include "Utilities/PDGQueries.h"
#include "Utilities/ControlSpeculation.h"

using namespace llvm;
using namespace liberty;

uint8_t liberty::disproveMemoryDep(Instruction *src, Instruction *dst,
                                   LoopAA::TemporalRelation FW,
                                   LoopAA::TemporalRelation RV,
                                   uint8_t depTypes, Loop *loop, LoopAA *aa) {

  uint8_t disprovedDeps = depTypes;
  if (!depTypes)
    // no dependences to disprove
    return disprovedDeps;

  if (!src->mayReadOrWriteMemory())
    return disprovedDeps;
  if (!dst->mayReadOrWriteMemory())
    return disprovedDeps;
  if (!src->mayWriteToMemory() && !dst->mayWriteToMemory())
    return disprovedDeps;

  bool loopCarried = FW != RV;

  // The remedies set will remain empty when no speculation is used
  Remedies R;

  // forward dep test
  LoopAA::ModRefResult forward = aa->modref(src, FW, dst, loop, R);

  if (!src->mayWriteToMemory())
    forward = LoopAA::ModRefResult(forward & (~LoopAA::Mod));
  if (!src->mayReadFromMemory())
    forward = LoopAA::ModRefResult(forward & (~LoopAA::Ref));

  if (LoopAA::NoModRef == forward)
    return disprovedDeps;

  // reverse dep test
  LoopAA::ModRefResult reverse = forward;

  if (loopCarried || src != dst)
    reverse = aa->modref(dst, RV, src, loop, R);

  if (!dst->mayWriteToMemory())
    reverse = LoopAA::ModRefResult(reverse & (~LoopAA::Mod));
  if (!dst->mayReadFromMemory())
    reverse = LoopAA::ModRefResult(reverse & (~LoopAA::Ref));

  if (LoopAA::NoModRef == reverse)
    return disprovedDeps;

  if (LoopAA::Ref == forward && LoopAA::Ref == reverse)
    return disprovedDeps; // RaR dep; who cares.

  // At this point, we know there is one or more of
  // a flow-, anti-, or output-dependence.

  bool RAW = (forward == LoopAA::Mod || forward == LoopAA::ModRef) &&
             (reverse == LoopAA::Ref || reverse == LoopAA::ModRef);
  bool WAR = (forward == LoopAA::Ref || forward == LoopAA::ModRef) &&
             (reverse == LoopAA::Mod || reverse == LoopAA::ModRef);
  bool WAW = (forward == LoopAA::Mod || forward == LoopAA::ModRef) &&
             (reverse == LoopAA::Mod || reverse == LoopAA::ModRef);

  // set to zero the bits for the reported deps by SCAF
  if (RAW) {
    disprovedDeps &= ~1;
  }
  if (WAW) {
    disprovedDeps &= ~(1 << 1);
  }
  if (WAR) {
    disprovedDeps &= ~(1 << 2);
  }

  // return the disproved dependences
  return disprovedDeps;
}

uint8_t liberty::disproveIntraIterationMemoryDep(Instruction *src,
                                                 Instruction *dst,
                                                 uint8_t depTypes, Loop *loop,
                                                 LoopAA *aa) {
  NoControlSpeculation noctrlspec;
  noctrlspec.setLoopOfInterest(loop->getHeader());
  // check if dst is reachable from src within the same iteration of the given
  // loop
  if (noctrlspec.isReachable(src, dst, loop))
    return disproveMemoryDep(src, dst, LoopAA::Same, LoopAA::Same, depTypes,
                             loop, aa);

  // if not reachable then all reported deps are disproved
  return depTypes;
}

uint8_t liberty::disproveLoopCarriedMemoryDep(Instruction *src,
                                              Instruction *dst,
                                              uint8_t depTypes, Loop *loop,
                                              LoopAA *aa) {
  // there is always a feasible path for inter-iteration deps
  // (there is a path from any node in the loop to the header
  //  and the header dominates all the nodes of the loops)

  // only need to check for aliasing and kill-flow

  return disproveMemoryDep(src, dst, LoopAA::Before, LoopAA::After, depTypes,
                           loop, aa);
}
