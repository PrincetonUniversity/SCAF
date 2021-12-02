#include "scaf/Utilities/CallBaseFactory.h"

using namespace llvm;

CallBase* liberty::getCallBase(Value *value) { return dyn_cast<CallBase>(value); }

const CallBase* liberty::getCallBase(const Value *value) {
  return getCallBase(const_cast<Value *>(value));
}

