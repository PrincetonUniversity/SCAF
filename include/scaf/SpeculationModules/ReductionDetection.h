#ifndef LLVM_LIBERTY_ANALYSIS_REDUCTION_DETECTION_H
#define LLVM_LIBERTY_ANALYSIS_REDUCTION_DETECTION_H

#include "llvm/IR/Instructions.h"
#include "llvm/Analysis/LoopInfo.h"

#include "scaf/SpeculationModules/Reduction.h"

#include "noelle/core/PDG.hpp"

#include<unordered_set>

namespace liberty
{
using namespace llvm;
using namespace SpecPriv;
using namespace arcana::noelle;

struct MinMaxReductionInfo {
  const CmpInst *cmpInst;
  const Value *minMaxValue;

  Reduction::Type type;

  const Instruction *minMaxInst;

  const Instruction *depInst;
  Reduction::Type depType;
  const Instruction *depUpdateInst;

  // Is the running min/max the first or second operand of the compare?
  bool isFirstOperand;

  // Does the compare return true or false when finding a new min/max?
  bool cmpTrueOnMinMax;

  // Other values that are live out with the min/max
  std::vector<Value*> reductionLiveOuts;

  /// List of edges affected by this reduction.
  using Edges = std::vector<DGEdge<Value *> *>;
  Edges edges;
};

struct ReductionDetection {

  bool isSumReduction(const Loop *loop, const Instruction *src,
                      const Instruction *dst, const bool loopCarried,
                      Reduction::Type &type);
  bool isMinMaxReduction(const Loop *loop, const Instruction *src,
                         const Instruction *dst, const bool loopCarried,
                         Reduction::Type &type, const Instruction **depInst,
                         SpecPriv::Reduction::Type &depType,
                         const Instruction **depUpdateInst,
                         const CmpInst **cmpInst);

  void findMinMaxRegReductions(Loop *loop, PDG *pdg);

  ~ReductionDetection() {
    for (auto i: minMaxReductions) {
      if (i.second)
        delete i.second;
    }
  }

private:
  std::unordered_map<const Instruction *, MinMaxReductionInfo *>
      minMaxReductions;
};
}

#endif
