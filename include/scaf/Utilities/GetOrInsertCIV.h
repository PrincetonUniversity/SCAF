#ifndef LLVM_LIBERTY_UTILITIES_GET_CIV_H
#define LLVM_LIBERTY_UTILITIES_GET_CIV_H

#include "llvm/Analysis/LoopInfo.h"
#include "llvm/IR/Instructions.h"

namespace liberty {
using namespace llvm;

PHINode *getOrInsertCanonicalInductionVariable(const Loop *loop);
} // namespace liberty

#endif

