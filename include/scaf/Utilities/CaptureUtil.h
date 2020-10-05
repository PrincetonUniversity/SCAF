#ifndef CAPTURE_UTIL_H
#define CAPTURE_UTIL_H

#include "llvm/ADT/DenseSet.h"
#include "llvm/IR/Value.h"

namespace liberty {

typedef llvm::DenseSet<const llvm::Value *> CaptureSet;

// Returns true if the value V is captured.
// If 'captures' is not null, then store all captures into that set.
// Otherwise, stop after the first capture is found.
bool findAllCaptures(const llvm::Value *V, CaptureSet *captures = NULL);
} // namespace liberty

#endif /* CAPTURE_UTIL_H */
