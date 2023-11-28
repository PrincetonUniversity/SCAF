#include "scaf/MemoryAnalysisModules/GetCallers.h"
#include "scaf/MemoryAnalysisModules/LoopAA.h" // for FULL_UNIVERSAL
#include "scaf/Utilities/CallBaseFactory.h"
#include "llvm/IR/Constants.h"

namespace liberty {
using namespace arcana::noelle;
bool getCallers(const Function *fcn, CallBaseList &callsitesOut) {
  bool addressCaptured = false;
  for (Value::const_user_iterator i = fcn->user_begin(), e = fcn->user_end();
       i != e; ++i) {
    const Value *v = *i;

    // ZY: 01/18/21
    // The user might node be the function call; but an argument of a callsite
    // Found in 526.blender
    // Remedy: double check called function before adding to callsitesOut
    //         However, if function pointers are used as arguments, how to
    //         guarantee fcn is not called through a virtual pointer?
    //         We can't unless we keep looking into another layer; this
    //         case seems rare, so we assume it's captured conservatively.

    const CallBase * cs = getCallBase(v);

    if (cs) {
      if (cs->getCalledFunction() == fcn) {
        callsitesOut.push_back(cs);
        continue;
      }
    }

    if (const ConstantExpr *cexp = dyn_cast<ConstantExpr>(v))
      if (cexp->isCast() && cexp->hasOneUse()) {
        cs = getCallBase(*cexp->user_begin());
        if (cs) {
          if (cs->getCalledFunction() == fcn) {
            callsitesOut.push_back(cs);
            continue;
          }
        }
      }

    addressCaptured = true;
  }

  if (addressCaptured)
    return false;

  return FULL_UNIVERSAL || fcn->hasLocalLinkage();
}
} // namespace liberty
