#ifndef CALL_SITE_FACTORY
#define CALL_SITE_FACTORY

#include "llvm/IR/Instructions.h"

namespace liberty {
llvm::CallBase* getCallBase(llvm::Value *value);
const llvm::CallBase* getCallBase(const llvm::Value *value);
} // namespace liberty

#endif /* CALL_SITE_FACTORY */
