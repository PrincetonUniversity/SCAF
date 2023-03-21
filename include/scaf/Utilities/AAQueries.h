#ifndef LLVM_LIBERTY_AA_QUERIES_H
#define LLVM_LIBERTY_AA_QUERIES_H

#include "llvm/Analysis/AliasAnalysis.h"
#include "llvm/Analysis/LoopInfo.h"
#include "llvm/Analysis/LoopPass.h"
#include "llvm/IR/CallSite.h"
#include "llvm/IR/DataLayout.h"
#include "llvm/IR/BasicBlock.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/Instructions.h"

#include "Assumptions.h"

#include <stdint.h>

namespace liberty {
using namespace llvm;
using namespace llvm::noelle;

class AAQuery {
public:

  AAQuery();
  ~AAQuery();

  static unsigned const UnknownSize = ~0u;

  //PartialAlias?
  enum AliasResult {
    NoAlias = 0,
    MayAlias = 3,
    MustAlias = 1
  };

  enum ModRefResult { NoModRef = 0, Ref = 1, Mod = 2, ModRef = 3 };

  enum DesiredAliasResult { DNoAlias = 0, DMustAlias = 2, DNoOrMustAlias = 3 };

  enum TemporalRelation { Before = 0, Same = 1, After = 2 };

  // Alias Analysis Query Interface
  // TODO: all arguments of CAF & context (scope)
  virtual AliasResult alias(const Value *ptrA, unsigned sizeA, TemporalRelation rel,
                            const Value *ptrB, unsigned sizeB, const Loop *L,
                            const uint8_t *C, Remedies &remeds,
                            DesiredAliasResult dAliasRes = DNoOrMustAlias);

  virtual ModRefResult modref(const Instruction *A, TemporalRelation rel, const Value *ptrB,
                            unsigned int sizeB, const Loop *L, const uint8_t *C, Remedies &remeds);

  virtual ModRefResult modref(const Instruction *A, TemporalRelation rel, const Instruction *B,
                            const Loop *L, const uint8_t *C, Remedies &R);

};

} // namespace liberty

#endif
