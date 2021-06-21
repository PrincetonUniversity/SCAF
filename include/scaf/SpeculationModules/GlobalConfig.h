#ifndef LLVM_LIBERTY_GLOBAL_CONFIG_H
#define LLVM_LIBERTY_GLOBAL_CONFIG_H

#include "llvm/Support/CommandLine.h"

using namespace llvm;

extern cl::opt<bool> EnableEdgeProf;
extern cl::opt<bool> EnableLamp;
extern cl::opt<bool> EnableSpecPriv;

#endif
