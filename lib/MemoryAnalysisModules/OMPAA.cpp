#define DEBUG_TYPE "omp-aa"

#include "scaf/MemoryAnalysisModules/OMPAA.h"
#include "llvm/Analysis/ValueTracking.h"

namespace liberty {
using namespace llvm;
using namespace llvm::noelle;

OMPAA::OMPAA() : ModulePass(ID) { }
OMPAA::~OMPAA() {}

LoopAA::AliasResult OMPAA::alias(const Value *ptrA, unsigned sizeA,
                                 TemporalRelation rel, const Value *ptrB,
                                 unsigned sizeB, const Loop *L, Remedies &R,
                                 DesiredAliasResult dAliasRes) {
  // If the pragma is on the parent loop, do nothing
  //FIXME: append parallel semantics to loop instead of individual instructions?
  if(auto parentLoop = L->getParentLoop()) {
    auto parentHead = parentLoop->getHeader();
    for(const auto &I : *parentHead) {
      auto parentPragmas = parseAnnotationsForInst(&I);
      if(parentPragmas.count("workshare")) {
        return LoopAA::alias(ptrA, sizeA, rel, ptrB, sizeB, L, R, dAliasRes);
      }
    }
  }
  // TODO: ptrA and ptrB should be in same loop. Is this condition checked elsewhere?
  const Instruction *instA, *instB;
  if ((instA = dyn_cast<Instruction>(ptrA)) && (instB = dyn_cast<Instruction>(ptrB))) {
      // Parse pragmas on each instruction
      std::unordered_set<std::string> pragmasA = parseAnnotationsForInst(instA);
      std::unordered_set<std::string> pragmasB = parseAnnotationsForInst(instB);
      // If no pragmas are detected, do nothing
      if(pragmasA.empty() || pragmasB.empty())
        return LoopAA::alias(ptrA, sizeA, rel, ptrB, sizeB, L, R, dAliasRes);

      //FIXME: added support needed for synchronization pragmas
      bool isUnconditionalA = isGuaranteedToExecuteForEveryIteration(instA, L);
      bool isUnconditionalB = isGuaranteedToExecuteForEveryIteration(instB, L);

      bool isParallelForA = pragmasA.count("workshare") && pragmasA.count("independent") && !pragmasA.count("reducible");
      bool isParallelForB = pragmasB.count("workshare") && pragmasB.count("independent") && !pragmasB.count("reducible");

      bool isSimpleA = !pragmasA.count("cummatative");
      bool isSimpleB = !pragmasB.count("cummatative");

      // check if at least one is a write
      if(isa<StoreInst>(instA) || isa<StoreInst>(instB)) {
        //FIXME: is unconditional needed here?
        if(isUnconditionalA && isUnconditionalB) {
          if(isParallelForA && isParallelForB) {
            if(isSimpleA || isSimpleB)
              return NoAlias;
          }
        }
      }

      //auto Op0 = dyn_cast<MDNode>(metaDataA->getOperand(0));
      //errs() << "Metadata detected at instruction A: " << *instA << "\n";
      //errs() << cast<MDString>(Op0->getOperand(0))->getString() << "\n";
    }
  return LoopAA::alias(ptrA, sizeA, rel, ptrB, sizeB, L, R, dAliasRes);
}

LoopAA::ModRefResult OMPAA::modref(const Instruction *A, TemporalRelation rel,
                                   const Value *ptrB, unsigned sizeB,
                                   const Loop *L, Remedies &R) {
  const Value* ptrA = cast<Value>(A);
  if(alias(cast<Value>(A), UnknownSize, rel, ptrB, sizeB, L, R) == NoAlias)
    return LoopAA::NoModRef;
  return LoopAA::modref(A, rel, ptrB, sizeB, L, R);
}

LoopAA::ModRefResult OMPAA::modref(const Instruction *A, TemporalRelation rel,
                                   const Instruction *B, const Loop *L,
                                   Remedies &R) {
  if(alias(cast<Value>(A), UnknownSize, rel, cast<Value>(B), UnknownSize, L, R) == NoAlias)
    return LoopAA::NoModRef;
  return LoopAA::modref(A, rel, B, L, R);
}


static RegisterPass<OMPAA>
    X("omp-aa", "Dependence information implied in OpenMP pragmas");
static RegisterAnalysisGroup<liberty::LoopAA> Y(X);
char OMPAA::ID = 0;

} // namespace liberty
