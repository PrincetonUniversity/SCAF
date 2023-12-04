#include "llvm/IR/DataLayout.h"
#include "llvm/IR/DerivedTypes.h"
#include "llvm/IR/Type.h"

#include "scaf/Utilities/GetSize.h"

using namespace llvm;

namespace liberty {

unsigned getSize(Type *type, const DataLayout *TD) {
  if (type->isSized()) {
    return TD ? TD->getTypeStoreSize(type) : ~0u;
  }
  return 0;
}

// Conservatively return the size of a value
unsigned getSize(const Value *value, const DataLayout *TD) {
  Type *type = value->getType();
  return getSize(type, TD);
}

unsigned getTargetSize(const Value *value, const DataLayout *TD) {
  Type *type = value->getType();

  // FIXME: getElementType is deprecated
  Type *targetType;
  if (auto seqType = dyn_cast<ArrayType>(type))
    targetType = seqType->getPointerElementType();
  if (auto seqType = dyn_cast<VectorType>(type))
    targetType = seqType->getPointerElementType();
  else {
    PointerType *pType = dyn_cast<PointerType>(type);
    assert(pType && "Must be a SequentialType or PointerType");
    targetType = pType->getPointerElementType();
  }

  return getSize(targetType, TD);
}
} // namespace liberty
