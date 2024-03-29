// Subloop-Combinator-AA
// Reasons about operations within sub-loops
// to improve intra-iteration results.
//
// This is desirable since we have several AAs
// which are good at inter-iteration queries.
//
// The reasoning is as follows:
//  Suppose you are quering modref results between
//  operations A,B within the SAME iteration of loop L.
//  Further, suppose that there is an immediate subloop L' of L
//  which contains both A and B.
//  Then A-modref-B iff A-modref-B within the same iteration
//  of L', or if A-modref-B across iterations of L'.
#define DEBUG_TYPE "subloop-combinator-aa"

#include "scaf/MemoryAnalysisModules/LoopAA.h"
#include "llvm/ADT/Statistic.h"

namespace liberty {
using namespace llvm;
using namespace arcana::noelle;

STATISTIC(numQueries, "Num queries");
STATISTIC(numEligible, "Num eligible");
STATISTIC(numTops, "Num tops");
STATISTIC(numBenefit, "Num queries which benefit");
STATISTIC(maybeBenefit, "Num queries which possibly benefit");

struct SubloopCombinatorAA : public ModulePass, public LoopAA {
  static char ID;
  SubloopCombinatorAA() : ModulePass(ID) {}

  virtual void *getAdjustedAnalysisPointer(AnalysisID PI) {
    if (PI == &LoopAA::ID)
      return (LoopAA *)this;
    return this;
  }

  virtual void getAnalysisUsage(AnalysisUsage &AU) const {
    LoopAA::getAnalysisUsage(AU);
    AU.setPreservesAll();
  }

  virtual StringRef getLoopAAName() const { return "subloop-combinator-aa"; }
  virtual StringRef getPassName() const {
    return "Analysis that reasons about subloops";
  }
  virtual SchedulingPreference getSchedulingPreference() const {
    return SchedulingPreference(Normal - 1);
  }

  virtual bool runOnModule(Module &M) {
    const DataLayout &DL = M.getDataLayout();
    InitializeLoopAA(this, DL);
    return false;
  }

  virtual ModRefResult modref(const Instruction *A, TemporalRelation T,
                              const Value *P2, unsigned S2, const Loop *L,
                              Remedies &R) {
    return LoopAA::modref(A, T, P2, S2, L, R); // chain
  }

  virtual ModRefResult modref(const Instruction *A, TemporalRelation T,
                              const Instruction *B, const Loop *L,
                              Remedies &R) {
    ++numQueries;

    if (!L || T != Same)
      return LoopAA::modref(A, T, B, L, R); // chain

    // Only care about queries where both
    // instructions are located within an
    // immediate subloop of L
    for (Loop::iterator i = L->begin(), e = L->end(); i != e; ++i) {
      Loop *subloop = *i;
      if (subloop->contains(A) && subloop->contains(B))
        return subloop_modref(A, T, B, L, subloop, R);
    }

    return LoopAA::modref(A, T, B, L, R); // chain
  }

private:
  ModRefResult subloop_modref(const Instruction *A, TemporalRelation T,
                              const Instruction *B, const Loop *L,
                              const Loop *subloop, Remedies &R) {
    ++numEligible;

    // Detect when more queries won't help.
    ModRefResult worst_case = ModRef;
    if (!A->mayWriteToMemory())
      worst_case = Ref;
    else if (!A->mayReadFromMemory())
      worst_case = Mod;

    const ModRefResult before = top(A, Before, B, subloop, R);
    if (before == worst_case) // bail-out
      return ModRefResult(worst_case & LoopAA::modref(A, T, B, L, R));

    const ModRefResult after = top(A, After, B, subloop, R);
    ModRefResult join = ModRefResult(before | after);
    if (join == worst_case) // bail-out
      return ModRefResult(worst_case & LoopAA::modref(A, T, B, L, R));

    const ModRefResult same = top(A, Same, B, subloop, R);
    join = ModRefResult(before | same | after);

    if (join == NoModRef) {
      // we don't know if chain would have returned NoModRef.
      ++maybeBenefit;
      return NoModRef;
    }

    const ModRefResult chain = LoopAA::modref(A, T, B, L, R);
    const ModRefResult meet = ModRefResult(join & chain);
    if (meet != chain)
      ++numBenefit;

    return meet;
  }

  ModRefResult top(const Instruction *A, TemporalRelation T,
                   const Instruction *B, const Loop *L, Remedies &R) {
    ++numTops;
    return getTopAA()->modref(A, T, B, L, R);
  }
};

static RegisterPass<SubloopCombinatorAA> X("subloop-combinator-aa",
                                           "Reason about subloops of loops");
static RegisterAnalysisGroup<liberty::LoopAA> Y(X);
char SubloopCombinatorAA::ID = 0;
} // namespace liberty
