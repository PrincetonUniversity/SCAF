#ifndef LLVM_LIBERTY_SPLIT_EDGE_H
#define LLVM_LIBERTY_SPLIT_EDGE_H

#include "llvm/Analysis/DominanceFrontier.h"
#include "llvm/Analysis/LoopInfo.h"
#include "llvm/IR/BasicBlock.h"
#include "llvm/IR/Dominators.h"

namespace liberty {
using namespace llvm;

/// Split an out edge from the basic block 'from'
/// by inserting a basic block in between them.  Update
/// terminators and phi instructions appropriately.
/// If a LoopInfo is provided, then update it so that the
/// any newly created basic blocks are members of the
/// appropriate loop.
/// Return the new basic block.
BasicBlock *split(BasicBlock *from, unsigned succno, StringRef prefix = "",
                  LoopInfo *updateLoopInfo = 0);

} // namespace liberty

#endif // LLVM_LIBERTY_SPLIT_EDGE_H
