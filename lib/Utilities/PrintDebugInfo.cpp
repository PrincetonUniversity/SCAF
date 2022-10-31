#include "scaf/Utilities/PrintDebugInfo.h"

#include "llvm/IR/DebugInfoMetadata.h"
#include "llvm/IR/Instruction.h"
#include "llvm/Support/raw_ostream.h"

using namespace llvm;

void liberty::printInstDebugInfo(Instruction *I) {
  const DebugLoc &debugLoc = I->getDebugLoc();
  if (debugLoc) {
    DIScope *scope = dyn_cast<DIScope>(debugLoc->getScope());
    if (scope) {
      std::string filename = scope->getFilename();
      errs() << " (filename:" << filename << ", line:";
    } else
      errs() << " (line:";

    errs() << debugLoc.getLine() << ", col:" << debugLoc.getCol() << ")";

    DILocation *loc = debugLoc.get();
    loc = loc->getInlinedAt();
    while (loc) {
      DIScope *scope = dyn_cast<DIScope>(loc->getScope());
      if (scope) {
        std::string filename = scope->getFilename();
        errs() << " (inlined at filename:" << filename << ", line:";
      } else {
        errs() << " (inlined at line:";
      }
      errs() << loc->getLine() << ", col:" << loc->getColumn() << ")";
      loc = loc->getInlinedAt();
    }
  }
}
