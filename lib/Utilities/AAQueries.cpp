#include "scaf/Utilities/AAQueries.h"

using namespace llvm;
using namespace llvm::noelle;
using namespace liberty;

AAQuery::AAQuery() {};

AAQuery::~AAQuery() {};

AAQuery::AliasResult AAQuery::alias(const Value *ptrA, unsigned sizeA,
                                    TemporalRelation rel, const Value *ptrB, unsigned sizeB,
                                    const Loop* L, const uint8_t *C, Remedies &remeds,
                                    DesiredAliasResult dAliasRes) {
  return MayAlias;
}

AAQuery::ModRefResult AAQuery::modref(const Instruction *A, TemporalRelation rel,
                                      const Value *ptrB, unsigned sizeB,
                                      const Loop *L, const uint8_t *C, Remedies &remeds) {
  return ModRef;
}

AAQuery::ModRefResult AAQuery::modref(const Instruction *A, TemporalRelation rel,
                                      const Instruction *B, const Loop *L,
                                      const uint8_t *C, Remedies &remeds) {
  return ModRef;
}
