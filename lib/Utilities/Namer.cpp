/***
 * Namer.cpp
 *
 * Generate ID for each instruction
 *
 * */

#define DEBUG_TYPE "namer"

// LLVM header files
#include "llvm/IR/LLVMContext.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/BasicBlock.h"
#include "llvm/IR/Instruction.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/Constants.h"
#include "llvm/Analysis/LoopInfo.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/IR/InstIterator.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/Transforms/Scalar.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/DerivedTypes.h"
#include "llvm/IR/GlobalVariable.h"

#include "scaf/Utilities/Metadata.h"
#include <list>

namespace liberty {
char Namer::ID = 0;
namespace {
static RegisterPass<Namer>
    RP("metadata-namer", "Generate unique IDs in Metadata for each instruction",
       false, false);
}


static std::string NamerMeta = "namer";
enum NamerMetaID {
  FCN_ID = 0,
  BB_ID = 1,
  INST_ID = 2,
};

Namer::Namer() : ModulePass(ID) {}

Namer::~Namer() { reset(); }

void Namer::reset() {
  pM = nullptr;
  funcId = 0;
  blkId = 0;
  instrId = 0;
}

void Namer::getAnalysisUsage(AnalysisUsage &AU) const {
  AU.addRequired<LoopInfoWrapperPass>();
  AU.setPreservesAll();
}

bool Namer::runOnModule(Module &M) {
  reset();
  pM = &M;
  LLVM_DEBUG(errs() << "\n\n\nEntering Metadata-Namer.\n");

  using FunList = Module::FunctionListType;
  using FunListIt = FunList::iterator;

  FunList &funcs = M.getFunctionList();
  bool modified = false;

  for (auto & func : funcs) {
    auto *f = (Function *)&func;
    modified |= runOnFunction(*f);
    funcId++;
  }

  return modified;
}

bool Namer::runOnFunction(Function &F) {
  LLVM_DEBUG(errs() << "function:" << F.getName() << "\n");
  LLVMContext &context = F.getContext();

  bool modified = false;

  for (auto & bb : F) {

    if (bb.getName().empty()) {
      bb.setName("bbName");
      modified = true;
    }

    for (auto & I : bb) {
      Instruction *inst = &I;

      Value *fcnV = ConstantInt::get(Type::getInt32Ty(context), funcId);
      Value *blkV = ConstantInt::get(Type::getInt32Ty(context), blkId);
      Value *instrV = ConstantInt::get(Type::getInt32Ty(context), instrId);

      Metadata *valuesArray[] = {ValueAsMetadata::get(fcnV),
                                 ValueAsMetadata::get(blkV),
                                 ValueAsMetadata::get(instrV)};
      ArrayRef<Metadata *> values(valuesArray, 3);
      MDNode *mdNode = MDNode::get(context, values);

      //  The liberty.namer metadata is just a list of every metadata we insert.
      //  It is wasteful, and nobody uses it.
      //  It creates *real* scalability problems down road. -NPJ
      //				NamedMDNode *namedMDNode =
      //pM->getOrInsertNamedMetadata("liberty.namer");
      //				namedMDNode->addOperand(mdNode);

      inst->setMetadata(NamerMeta, mdNode);
      instrId++;
    }
    blkId++;
  }

  return modified;
}

Value *getIdValue(const Instruction *I, NamerMetaID id) {
  if (I == nullptr)
    return nullptr;

  MDNode *md = I->getMetadata(NamerMeta);
  if (md == nullptr)
    return nullptr;
  ValueAsMetadata *vsm = dyn_cast<ValueAsMetadata>(md->getOperand(id));
  auto *vFB = cast<ConstantInt>(vsm->getValue());
  const int f_v = vFB->getSExtValue();
  return ConstantInt::get(vFB->getType(), f_v);
}

Value *Namer::getFuncIdValue(Instruction *I) {
  return getIdValue(I, FCN_ID);
}

Value *Namer::getBlkIdValue(Instruction *I) {
  return getIdValue(I, BB_ID);
}

Value *Namer::getInstrIdValue(Instruction *I) {
  return getInstrIdValue((const Instruction *)I);
}

Value *Namer::getInstrIdValue(const Instruction *I) {
  return getIdValue(I, INST_ID);
}

int Namer::getFuncId(Function *F) {
  // need to get 
  //

  if (!F || F->isDeclaration())
  {
    return -1;
  }

  if (F->getInstructionCount() == 0)
  {
    return -1;
  }

  
  auto inst = F->getEntryBlock().getFirstNonPHI();

  return getFuncId(inst);
}

int Namer::getFuncId(Instruction *I) {
  Value *v = getFuncIdValue(I);
  if (v == NULL)
    return -1;
  ConstantInt *cv = (ConstantInt *)v;
  return (int)cv->getSExtValue();
}

int Namer::getBlkId(BasicBlock *B) {
  if (!B)
  {
    return -1;
  }
  auto inst = B->getFirstNonPHI();
  return getBlkId(inst);
}

int Namer::getBlkId(Instruction *I) {
  Value *v = getBlkIdValue(I);
  if (v == NULL) {
    return -1;
  }
  ConstantInt *cv = (ConstantInt *)v;
  auto blkId = cv->getSExtValue();
  auto blkIdInt = (int)blkId;
  return blkIdInt;
}

int Namer::getInstrId(Instruction *I) {
  Value *v = getInstrIdValue(I);
  if (v == NULL)
    return -1;
  ConstantInt *cv = (ConstantInt *)v;
  return (int)cv->getSExtValue();
}

int Namer::getInstrId(const Instruction *I) {
  Value *v = getInstrIdValue(I);
  if (v == NULL)
    return -1;
  ConstantInt *cv = (ConstantInt *)v;
  return (int)cv->getSExtValue();
}
} // namespace liberty
