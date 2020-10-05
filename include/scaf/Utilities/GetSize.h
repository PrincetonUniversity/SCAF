#ifndef GETSIZE_H
#define GETSIZE_H

#include "llvm/IR/DataLayout.h"
#include "llvm/IR/Type.h"
#include "llvm/IR/Value.h"

namespace liberty {
// Conservatively return the size of a value
unsigned getSize(llvm::Type *type, const llvm::DataLayout *TD);
unsigned getSize(const llvm::Value *value, const llvm::DataLayout *TD);
unsigned getTargetSize(const llvm::Value *value, const llvm::DataLayout *TD);
} // namespace liberty

#endif /* GETSIZE_H */
