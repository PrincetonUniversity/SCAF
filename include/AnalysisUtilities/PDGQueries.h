#ifndef LLVM_LIBERTY_PDG_QUERIES_H
#define LLVM_LIBERTY_PDG_QUERIES_H

#include "llvm/Analysis/LoopInfo.h"
#include "llvm/IR/BasicBlock.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/Instructions.h"

#include "MemoryAnalysisModules/LoopAA.h"

#include <stdint.h>

namespace liberty {

// Given a loop-aware analysis aa, try to disprove loop-carried or
// intra-iteration dependences from src to dst for any reported type (least
// significant bit set for RAW, 2nd bit set for WAW, 3rd bit for WAR).
// Returns the types of deps (any combination of RAW, WAW, WAR) that were
// disproven.

uint8_t disproveMemoryDep(Instruction *src, Instruction *dst,
                          LoopAA::TemporalRelation FW,
                          LoopAA::TemporalRelation RV, uint8_t depTypes,
                          Loop *loop, LoopAA *aa);

uint8_t disproveLoopCarriedMemoryDep(Instruction *src, Instruction *dst,
                                     uint8_t depTypes, Loop *loop,
                                     liberty::LoopAA *aa);

uint8_t disproveIntraIterationMemoryDep(Instruction *src, Instruction *dst,
                                        uint8_t depTypes, Loop *loop,
                                        liberty::LoopAA *aa);

} // namespace liberty

#endif
