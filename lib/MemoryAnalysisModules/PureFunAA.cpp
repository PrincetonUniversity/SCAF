#define DEBUG_TYPE "pure-fun-aa"

#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/SCCIterator.h"
#include "llvm/ADT/SmallPtrSet.h"
#include "llvm/Analysis/Passes.h"
#include "llvm/Analysis/ValueTracking.h"
#include "llvm/IR/CallSite.h"
#include "llvm/IR/InstIterator.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/IntrinsicInst.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/raw_ostream.h"

#include "scaf/MemoryAnalysisModules/PureFunAA.h"
#include "scaf/Utilities/CallSiteFactory.h"
#include "scaf/Utilities/GetSize.h"

#include "RefineCFG.h"

using namespace llvm;
using namespace llvm::noelle;

namespace liberty {

static bool isReadOnlyProp(const Instruction *inst) {
  const DataLayout &td = inst->getModule()->getDataLayout();
  if (const MemIntrinsic *mem = dyn_cast<MemIntrinsic>(inst)) {
    const Value *obj = GetUnderlyingObject(mem->getDest(), td);
    // if( !obj )
    //  return false;
    if (obj)
      if (const AllocaInst *alloca = dyn_cast<AllocaInst>(obj))
        if (alloca->getParent() == inst->getParent())
          return true;
    // return false;
  } else if (const StoreInst *store = dyn_cast<StoreInst>(inst)) {
    const Value *obj = GetUnderlyingObject(store->getPointerOperand(), td);
    // if( !obj )
    //  return false;
    if (obj)
      if (const AllocaInst *alloca = dyn_cast<AllocaInst>(obj))
        if (alloca->getParent() == inst->getParent())
          return true;
    // return false;
  }
  // return true;
  return !inst->mayWriteToMemory();
}

static bool isLocalProp(const Value *value) {
  const User *user = dyn_cast<User>(value);
  if (!user) {
    return true;
  }

  if (isa<Function>(user)) {
    return true;
  }

  if (isa<GlobalValue>(user)) {
    return false;
  }

  for (unsigned i = 0; i < user->getNumOperands(); ++i) {
    const Value *operand = user->getOperand(i);
    if (!isa<Instruction>(operand) && !isLocalProp(operand)) {
      return false;
    }
  }

  return true;
}

static bool isLocalProp(const Instruction *inst) {

  for (unsigned i = 0; i < inst->getNumOperands(); ++i) {
    const Value *value = inst->getOperand(i);
    if (!isLocalProp(value)) {
      return false;
    }
  }

  if (PureFunAA::isBadDeref(inst)) {
    return false;
  }

  return true;
}

bool PureFunAA::isBadDeref(const Instruction *inst) {
  // %p1 = gep arg xxx
  // %0 = load %p1 ;load a pointer
  // %p2 = gep %0 xxx
  // load %p2 ; this load is not within %p1
  const Function *fcn = inst->getParent()->getParent();
  // load and store; the memory address comes from out of arg
  const Value *basePtr = nullptr;
  if (auto *load = dyn_cast<LoadInst>(inst)) {
    basePtr = load->getPointerOperand()->stripInBoundsOffsets();
  }
  if (auto *store = dyn_cast<StoreInst>(inst)) {
    basePtr = store->getPointerOperand()->stripInBoundsOffsets();
  }

  if (basePtr) {
    for (auto &&arg : fcn->args()) {
      if (basePtr == &arg) {
        return false;
      }
    }
    // all others are bad
    return true;
  }

  // check all args for a callsite
  if (auto *cb = dyn_cast<CallBase>(inst)) {
    for (auto &&arg2 : cb->args()) {
      if (!arg2->getType()->isPointerTy()) {
        continue;
      }

      bool argBad = true;
      for (auto &&arg : fcn->args()) {
        if (arg2->stripInBoundsOffsets() == &arg) {
          argBad = false;
          break;
        }
      }
      if (argBad) {
        return true;
      }
    }
  }

  return false;
}

PureFunAA::SCCNum PureFunAA::getSCCNum(const SCC &scc) const {

  for (SCCIt it = scc.begin(); it != scc.end(); ++it) {

    const Function *fun = (*it)->getFunction();
    if (fun) {

      FunToSCCMapIt sccId = sccMap.find(fun);
      assert(sccId != sccMap.end());

      return sccId->second;
    }
  }

  return ~0U;
}

PureFunAA::SCCNum PureFunAA::getSCCNum(const Function *fun) const {
  FunToSCCMapIt sccId = sccMap.find(fun);
  return sccId != sccMap.end() ? sccId->second : ~0U;
}

bool PureFunAA::isRecursiveProperty(const Function *fun,
                                    const SCCNumSet &trueSet,
                                    const SCCNumSet &falseSet,
                                    const StringSet &knownFunSet,
                                    Property property) const {
  /* errs() << "in isRecursiveProperty()\n"; */
  if (!fun) {
    return false;
  }

  FunToSCCMapIt sccId = sccMap.find(fun);
  if (sccId != sccMap.end()) {
    if (trueSet.count(sccId->second)) {
      return true;
    }

    if (falseSet.count(sccId->second)) {
      return false;
    }
  }

  if (knownFunSet.count(fun->getName().str().c_str())) {
    return true;
  }

  StringRef fcn_name = fun->getName();
  for (StringRef knownFun : knownFunSet) {
    if (!knownFun.empty() && fcn_name.startswith(knownFun)) {
      return true;
    }
  }

  if (fun->isDeclaration()) {
    return false;
  }

  for (const_inst_iterator inst = inst_begin(fun); inst != inst_end(fun);
       ++inst) {

    // if the value is projected from the function argument
    if (!property(&*inst)) {
      return false;
    }

    const CallSite call =
        liberty::getCallSite(const_cast<Instruction *>(&*inst));
    if (call.getInstruction()) {
      const Value *value = call.getCalledValue()->stripPointerCasts();

      const Function *callee = dyn_cast<Function>(value);
      if (!callee) {
        return false;
      }

      if (getSCCNum(fun) != getSCCNum(callee) &&
          !isRecursiveProperty(callee, trueSet, falseSet, knownFunSet,
                               property)) {
        return false;
      }
    }
  }
  return true;
}

void PureFunAA::runOnSCC(const SCC &scc) {

  for (SCCIt it = scc.begin(); it != scc.end(); ++it) {
    const Function *fun = (*it)->getFunction();
    if (fun)
      sccMap[fun] = sccCount;
  }

  for (SCCIt it = scc.begin(); it != scc.end(); ++it) {
    const Function *fun = (*it)->getFunction();
    if (fun) {
      if (!isReadOnly(fun)) {
        writeSet.insert(sccCount);
      }

      if (!isLocal(fun)) {
        globalSet.insert(sccCount);
      }
    }
  }

  if (!writeSet.count(sccCount)) {
    readOnlySet.insert(sccCount);
  }

  if (!globalSet.count(sccCount)) {
    localSet.insert(sccCount);
  }

  assert((!readOnlySet.count(sccCount) || !writeSet.count(sccCount)) &&
         "SCC ReadOnly and Writes!");

  assert((!localSet.count(sccCount) || !globalSet.count(sccCount)) &&
         "SCC Local and Global");

  ++sccCount;
}

static unsigned getArgSize(const ImmutableCallSite CS) {
  const IntrinsicInst *II = dyn_cast<IntrinsicInst>(CS.getInstruction());
  unsigned Len = ClassicLoopAA::UnknownSize;
  if (II != 0)
    switch (II->getIntrinsicID()) {
    default:
      break;
    case Intrinsic::memcpy:
    case Intrinsic::memmove: {
      if (ConstantInt *LenCI = dyn_cast<ConstantInt>(II->getArgOperand(2)))
        Len = LenCI->getZExtValue();
      break;
    }
    case Intrinsic::memset:
      // Since memset is 'accesses arguments' only, the LoopAA base class
      // will handle it for the variable length case.
      if (ConstantInt *LenCI = dyn_cast<ConstantInt>(II->getArgOperand(2))) {
        Len = LenCI->getZExtValue();
      }
      break;
    }

  return Len;
}

bool PureFunAA::argumentsAlias(const ImmutableCallSite CS1,
                               const ImmutableCallSite CS2, LoopAA *aa,
                               const DataLayout *TD, Remedies &R) {

  typedef ImmutableCallSite::arg_iterator ArgIt;
  for (ArgIt arg = CS1.arg_begin(); arg != CS1.arg_end(); ++arg) {
    if ((*arg)->getType()->isPointerTy()) {

      // Special considertion
      unsigned argSize = getArgSize(CS1);
      if (argSize == UnknownSize)
        argSize = liberty::getTargetSize(*arg, TD);

      if (argumentsAlias(CS2, *arg, argSize, aa, TD, R)) {
        return true;
      }
    }
  }

  return false;
}

bool PureFunAA::argumentsAlias(const ImmutableCallSite CS, const Value *P,
                               const unsigned Size, LoopAA *aa,
                               const DataLayout *TD, Remedies &R) {

  for (unsigned i = 0; i < CS.arg_size(); ++i) {
    const Value *arg = CS.getArgument(i);

    if (arg->getType()->isPointerTy()) {

      // add check here for argument attribute



      unsigned argSize = getArgSize(CS);
      if (argSize == UnknownSize)
        argSize = liberty::getTargetSize(arg, TD);

      if (aa->alias(P, Size, Same, arg, argSize, NULL, R)) {
        return true;
      }
    }
  }
  LLVM_DEBUG(errs() << "\t  Arguments do not alias\n");
  return false;
}

PureFunAA::PureFunAA() : ModulePass(ID), sccCount(0) {
  if (!pureFunSet.size()) {
    for (int i = 0; !pureFunNames[i].empty(); ++i) {
      pureFunSet.insert(pureFunNames[i]);
      localFunSet.insert(pureFunNames[i]);
    }

    for (int i = 0; !localFunNames[i].empty(); ++i) {
      localFunSet.insert(localFunNames[i]);
    }

    for (int i = 0; !noMemFunNames[i].empty(); ++i) {
      noMemFunSet.insert(noMemFunNames[i]);
    }
  }

  LLVM_DEBUG(errs() << "Known pure functions: " << pureFunSet.size() << "\n");
  LLVM_DEBUG(errs() << "Known local functions: " << localFunSet.size() << "\n");
}

bool PureFunAA::runOnModule(Module &M) {
  const DataLayout &DL = M.getDataLayout();
  InitializeLoopAA(this, DL);

  CallGraph &CG = getAnalysis<CallGraphWrapperPass>().getCallGraph();
  for (scc_iterator<CallGraph *> CGI = scc_begin(&CG), E = scc_end(&CG);
       CGI != E; ++CGI) {
    runOnSCC(*CGI);
  }

  return false;
}

bool PureFunAA::isReadOnly(const Function *fun) const {
/*
 *   // FIXME: for serverless exps
 *   if (fun->getName().find("Aws") != std::string::npos) {
 *     return true;
 *   }
 * 
 *   if (fun->getName().find("basic_ostream") != std::string::npos) {
 *     return true;
 *   }
 * 
 *   if (fun->getName().find("basic_string") != std::string::npos) {
 *     return true;
 *   }
 * 
 *   if (fun->getName().find("basic_ios") != std::string::npos) {
 *     return true;
 *   }
 * 
 *   if (fun->getName().find("_ZNSolsEPFRSoS_E") != std::string::npos) {
 *     return true;
 *   }
 */
  
  if (fun->hasFnAttribute(Attribute::ReadOnly))
    return true;
  return isRecursiveProperty(fun, readOnlySet, writeSet, pureFunSet,
                             isReadOnlyProp);
}

bool PureFunAA::isLocal(const Function *fun) const {

/*
 *    // FIXME: for serverless exps
 *   if (fun->getName().find("Aws") != std::string::npos) {
 *     return true;
 *   }
 * 
 *   if (fun->getName().find("basic_ostream") != std::string::npos) {
 *     return true;
 *   }
 * 
 *   if (fun->getName().find("basic_string") != std::string::npos) {
 *     return true;
 *   }
 *   if (fun->getName().find("basic_ios") != std::string::npos) {
 *     return true;
 *   }
 * 
 *   if (fun->getName().find("_ZNSolsEPFRSoS_E") != std::string::npos) {
 *     return true;
 *   }
 */
  if (fun->hasFnAttribute(Attribute::ArgMemOnly))
    return true;
  return isRecursiveProperty(fun, localSet, globalSet, localFunSet,
                             isLocalProp);
}

bool PureFunAA::isPure(const Function *fun) const {
  return isReadOnly(fun) && isLocal(fun);
}

static Function *getCalledFunction(CallSite CS) {
  return dyn_cast<Function>(CS.getCalledValue()->stripPointerCasts());
}

PureFunAA::ModRefResult PureFunAA::getModRefInfo(CallSite CS1,
                                                 TemporalRelation Rel,
                                                 CallSite CS2, const Loop *L,
                                                 Remedies &R) {

  const Function *fun1 = getCalledFunction(CS1);
  const Function *fun2 = getCalledFunction(CS2);

  LLVM_DEBUG(errs() << "\tpure-fun-aa looking at " << *(CS1.getInstruction())
                    << " to " << *(CS2.getInstruction()) << "\n");

  if (!fun1 || !fun2) {
    return ModRef;
  }

  if (noMemFunSet.count(fun1->getName().str().c_str()) ||
      fun1->hasFnAttribute(Attribute::ReadNone)) {
    return NoModRef;
  }

  Remedies tmpR;

  LoopAA *aa = getTopAA();
  const DataLayout *TD = getDataLayout();

  // if(isLocal(fun1) && isLocal(fun2) && !argumentsAlias(CS1, CS2, aa, TD) &&
  //   !argumentsAlias(CS1, CS2.getInstruction(), aa, TD) &&
  //   !argumentsAlias(CS2, CS1.getInstruction(), aa, TD)) {
  if (isLocal(fun1) && isLocal(fun2) &&
      !argumentsAlias(CS1, CS2, aa, TD, tmpR) &&
      !argumentsAlias(CS2, CS1, aa, TD, tmpR)) {
    LLVM_DEBUG(errs() << "\t    pure-fun-aa returning NoModRef 1\n");
    for (auto remed : tmpR)
      R.insert(remed);
    return NoModRef;
  }

  // Could the first function write to the second function's read set?
  if (isReadOnly(fun1)) {
    return Ref;
  }

  return ModRef;
}

PureFunAA::ModRefResult PureFunAA::getModRefInfo(CallSite CS,
                                                 TemporalRelation Rel,
                                                 const Pointer &P,
                                                 const Loop *L, Remedies &R) {

  const Value *Ptr = P.ptr;
  const unsigned Size = P.size;

  const Function *fun = getCalledFunction(CS);
  if (!fun) {
    return ModRef;
  }

  if (noMemFunSet.count(fun->getName().str().c_str()) ||
      fun->hasFnAttribute(Attribute::ReadNone)) {
    return NoModRef;
  }

  Remedies tmpR;

  LLVM_DEBUG(errs() << "\tpure-fun-aa looking at " << *(CS.getInstruction())
                    << " to " << *Ptr << "\n");

  LoopAA *AA = getTopAA();
  const DataLayout *TD = getDataLayout();
  if (isLocal(fun) && !argumentsAlias(CS, Ptr, Size, AA, TD, tmpR) &&
      !AA->alias(CS.getInstruction(), Size, Rel, Ptr, Size, L, tmpR)) {

    LLVM_DEBUG(
        errs() << "\t    result of query "
               << AA->alias(CS.getInstruction(), Size, Rel, Ptr, Size, L, tmpR)
               << "\n");
    LLVM_DEBUG(errs() << "\t    pure-fun-aa returning NoModRef 2\n");
    for (auto remed : tmpR)
      R.insert(remed);
    return NoModRef;
  }

  if (isReadOnly(fun)) {
    return Ref;
  }

  return ModRef;
}

void PureFunAA::getAnalysisUsage(AnalysisUsage &AU) const {
  LoopAA::getAnalysisUsage(AU);
  AU.addRequired<CallGraphWrapperPass>();
  AU.addRequired<RefineCFG>();
  AU.setPreservesAll(); // Does not transform code
}

/// getAdjustedAnalysisPointer - This method is used when a pass implements an
/// analysis interface through multiple inheritance.  If needed, it should
/// override this to adjust the this pointer as needed for the specified pass
/// info.
void *PureFunAA::getAdjustedAnalysisPointer(AnalysisID PI) {
  if (PI == &LoopAA::ID)
    return (LoopAA *)this;
  return this;
}

char PureFunAA::ID = 0;

StringRef const PureFunAA::pureFunNames[] = {
#include "scaf/MemoryAnalysisModules/MultiTypePureFun.h"
#include "scaf/MemoryAnalysisModules/PureFun.h"
    ""};

StringRef const PureFunAA::localFunNames[] = {
#include "LocalFun.h"
    ""};

StringRef const PureFunAA::noMemFunNames[] = {
#include "NoMemFun.h"
    ""};

PureFunAA::StringSet PureFunAA::pureFunSet;
PureFunAA::StringSet PureFunAA::localFunSet;
PureFunAA::StringSet PureFunAA::noMemFunSet;

static RegisterPass<PureFunAA>
    X("pure-fun-aa", "Alias analysis for pure functions", false, true);
static RegisterAnalysisGroup<liberty::LoopAA> Y(X);

} // namespace liberty

