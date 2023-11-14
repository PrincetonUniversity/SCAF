#pragma once

#include "llvm/Pass.h"
#include "llvm/IR/BasicBlock.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/Module.h"

#include "scaf/MemoryAnalysisModules/LoopAA.h"
#include "scaf/MemoryAnalysisModules/SimpleAA.h"
#include "scaf/Utilities/ControlSpeculation.h"
#include "scaf/Utilities/LoopDominators.h"

#include "PDG.h"

#include <cstdint>
#include <unordered_set>
#include <unordered_map>

//using namespace llvm;
//using namespace llvm::noelle;
using namespace liberty;

namespace llvm {
struct PDGBuilder : public ModulePass {
public:
  static char ID;
  PDGBuilder() : ModulePass(ID) {
  }
  virtual ~PDGBuilder() {}

  // bool doInitialization (Module &M) override ;
  void getAnalysisUsage(AnalysisUsage &AU) const override;
  bool runOnModule(Module &M) override;
  void runOnFunction(Function &F);

  std::unique_ptr<PDG> getLoopPDG(Loop *loop, LoopAA* aa);

private:
  unsigned loopCount = 0;
  const DataLayout *DL;
  NoControlSpeculation noctrlspec;

  void constructEdgesFromMemory(PDG &pdg, Loop *loop, LoopAA *aa);

  uint8_t queryMemoryDep(Instruction *src, Instruction *dst,
                      LoopAA::TemporalRelation FW, LoopAA::TemporalRelation RV,
                      Loop *loop, LoopAA *aa, PDG &pdg);

  uint8_t queryLoopCarriedMemoryDep(Instruction *src, Instruction *dst, Loop *loop,
                                 LoopAA *aa, PDG &pdg);

  uint8_t queryIntraIterationMemoryDep(Instruction *src, Instruction *dst,
                                    Loop *loop, LoopAA *aa, PDG &pdg);

};
} // namespace llvm