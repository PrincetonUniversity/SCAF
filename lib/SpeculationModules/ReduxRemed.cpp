#define DEBUG_TYPE "redux-remed"

#include "scaf/MemoryAnalysisModules/Introspection.h"

#include "llvm/IR/IntrinsicInst.h"
#include "llvm/ADT/Statistic.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/Analysis/ScalarEvolutionExpressions.h"
#include "llvm/Transforms/Utils/LoopUtils.h"

#include "scaf/SpeculationModules/ReduxRemed.h"

namespace liberty
{
using namespace llvm;
using namespace arcana::noelle;
using namespace SpecPriv;

STATISTIC(numRegQueries,                      "Num register deps queries");
STATISTIC(numRegDepsRemovedSumRedux,          "Num reg deps removed with sum reduction");
STATISTIC(numRegDepsRemovedMinMaxRedux,       "Num reg deps removed with min/max reduction");
STATISTIC(numRegDepsRemovedLLVMRedux,         "Num reg deps removed with llvm's reduction identification");
STATISTIC(numRegDepsRemovedNoelleRedux,       "Num reg deps removed with noelle redux");
STATISTIC(numRegDepsRemovedRedux,             "Num reg deps removed with liberty redux");
STATISTIC(numCondRegDepsRemoved,              "Num reg deps removed with cond redux");
STATISTIC(numMemDepsRemovedRedux,             "Num mem deps removed");

void ReduxRemedy::apply(Task *task) {
  // TODO: transfer the code for application of redux here.
}

bool ReduxRemedy::compare(const Remedy_ptr rhs) const {
  std::shared_ptr<ReduxRemedy> reduxRhs =
      std::static_pointer_cast<ReduxRemedy>(rhs);
  if (this->liveOutV == nullptr && reduxRhs->liveOutV == nullptr)
    return this->reduxSCC < reduxRhs->reduxSCC;
  else if (this->liveOutV != nullptr && reduxRhs->liveOutV != nullptr)
    return this->liveOutV < reduxRhs->liveOutV;
  else
    return (this->liveOutV == nullptr);
}

bool ReduxRemediator::isRegReductionPHI(Instruction *I, Loop *l) {
  PHINode *phi = dyn_cast<PHINode>(I);
  if (phi == nullptr)
    return false;
  if (l->getHeader() != I->getParent())
    return false;
  // check if result is cached
  if (regReductions.count(I))
    return true;

  std::set<PHINode*> ignore;
  VSet phis, binops, cmps, brs, liveOuts;
  BinaryOperator::BinaryOps opcode;
  Reduction::Type type;
  Value *initVal = 0;
  if ( Reduction::isRegisterReduction(
        *se, l, phi, nullptr, ignore, /*Inputs*/
        type, opcode, phis, binops, cmps, brs, liveOuts, initVal) /*Outputs*/
     )
  {
    Instruction *liveOutV = phi;
    regReductions.insert(liveOutV);

    errs() << "Found a register reduction:\n"
           << "          PHI: " << *phi << '\n'
           << "      Initial: " << *initVal << '\n'
           << "    Internals:\n";
    for(VSet::iterator i=phis.begin(), e=phis.end(); i!=e;  ++i)
      errs() << "            o " << **i << '\n';
    errs() << "      Updates:\n";
    for(VSet::iterator i=binops.begin(), e=binops.end(); i!=e;  ++i)
      errs() << "            o " << **i << '\n';

    errs() << "    Live-outs:\n";
    for(VSet::iterator i=liveOuts.begin(), e=liveOuts.end(); i!=e;  ++i)
      errs() << "            o " << **i << '\n';

      return true;
  }
  return false;
}

// address scenarios like the following in 179.art
// tresult = 1;
// for (..) {
// ... (noone reads tresult)
//  if (..)
//    tresult = 0;
// }
//
// In loop's LLVM IR we get:
// %tresult.0 = phi i32 [ %tresult.1, %for.end101 ], [ 1, %for.end78 ]
// %tresult.1 = select i1 %cmp120, i32 0, i32 %tresult.0
//
// TODO: expand this detection to handle the conditional reg reduction in 175.vpr
// for now this detection is focused on the scenario described above
bool ReduxRemediator::isConditionalReductionPHI(const Instruction *I,
                                                const Loop *l) const {
  const PHINode *phi = dyn_cast<PHINode>(I);
  if (!phi)
    return false;
  if (l->getHeader() != I->getParent())
    return false;

  unsigned usesInsideLoop = 0;
  const Instruction *loopUserI = nullptr;
  for (auto user: phi->users()) {
    const Instruction *userI = dyn_cast<Instruction>(user);
    if (!userI)
      return false;
    if (l->contains(userI)) {
      ++usesInsideLoop;
      loopUserI = userI;
    }
  }

  if (usesInsideLoop != 1)
    return false;

  const SelectInst *updateI = dyn_cast<SelectInst>(loopUserI);
  if (!updateI)
    return false;

  if (!updateI->hasOneUse() || updateI->user_back() != (User *)phi)
    return false;

  if (updateI->getTrueValue() != (Value *)phi && updateI->getFalseValue() != (Value *)phi)
    return false;

  const Value *newV = (updateI->getTrueValue() == (Value *)phi)
                          ? updateI->getFalseValue()
                          : updateI->getTrueValue();

  if (!newV || !l->isLoopInvariant(newV))
    return false;

  return true;
}

void ReduxRemediator::findMinMaxRegReductions(Loop *l) {
  reduxdet.findMinMaxRegReductions(l, pdg);
}

void ReduxRemediator::findMemReductions(Loop *l) {

  std::set<Value *> visitedAccums;
  const std::vector<Loop *> subloops = l->getSubLoops();

  for (Loop::block_iterator bbi = l->block_begin(), bbe = l->block_end();
       bbi != bbe; ++bbi) {
    BasicBlock *bb = *bbi;
    // Exclude instructions in one of subloops
    bool withinSubloop = false;
    for (auto &sl : subloops) {
      if (sl->contains(bb)) {
        withinSubloop = true;
        break;
      }
    }
    if (withinSubloop)
      continue;

    for (BasicBlock::iterator i = bb->begin(), e = bb->end(); i != e; ++i) {
      LoadInst *load = dyn_cast<LoadInst>(i);
      if (!load)
        continue;

      Value *accum = load->getPointerOperand();
      if (visitedAccums.count(accum))
        continue;
      visitedAccums.insert(accum);

      // Pointer to accumulator must be loop invariant
      if (!l->isLoopInvariant(accum))
        continue;

      const BinaryOperator *add = nullptr;
      const CmpInst *cmp = nullptr;
      const BranchInst *br = nullptr;
      const StoreInst *store = nullptr;
      const Reduction::Type type =
          Reduction::isReductionLoad(load, &add, &cmp, &br, &store);
      if (!type)
        continue;

      // Looks like a reduction.
      // Next, we will use static analysis to ensure that
      //  for every other memory operation in this loop, either:
      //   a. the operation is a reduction operation of the same type, or
      //   b. the operation does not access this accumulator.
      if (!Reduction::allOtherAccessesAreReduction(l, type, accum, loopAA))
        continue;

      // Now this is a reduction
      // This should be either add reduction or min/max reduction
      memReductions.insert(store);
    }
  }
}

bool ReduxRemediator::isMemReduction(const Instruction *I) {
  const StoreInst *sI = dyn_cast<StoreInst>(I);
  if (!sI)
    return false;
  if (memReductions.count(sI))
    return true;

  return false;
}

// there can be RAW reg deps
Remediator::RemedResp ReduxRemediator::regdep(const Instruction *A,
                                              const Instruction *B,
                                              bool loopCarried, const Loop *L) {

  ++numRegQueries;

  Remediator::RemedResp remedResp;
  // conservative answer
  remedResp.depRes = DepResult::Dep;

  if (!loopCarried)
    return remedResp;

  auto remedy = std::make_shared<ReduxRemedy>();
  //remedy->cost = DEFAULT_REDUX_REMED_COST;
  remedy->cost = 0;

  //const bool loopCarried = (B->getParent() == L->getHeader() && isa<PHINode>(B));

  // if fast math for floating point is not allowed it enable it for current
  // function
  // TODO: eventually could actually ask the user, but that will lower the
  // preference for this pass. Alternatively, could also compile with -ffast-math
  // flag. Compiling with the flag will produce more optimal code overall and
  // should eventually be used.

  Instruction *ncA = const_cast<Instruction*>(A);
  Instruction *ncB = const_cast<Instruction*>(B);
  Loop *ncL = const_cast<Loop *>(L);
  Reduction::Type type;
  remedy->depInst = nullptr;
  remedy->depType = Reduction::NotReduction;
  remedy->depUpdateInst = nullptr;

  /*
  Function *F = ncA->getParent()->getParent();
  if (F->getFnAttribute("no-nans-fp-math").getValueAsString() == "false") {
    errs() << "THe no-nans-fp-math flag was not set!\n";
    F->addFnAttr("no-nans-fp-math", "true");
  }
  if (F->getFnAttribute("unsafe-fp-math").getValueAsString() == "false") {
    errs() << "THe unsafe-fp-math flag was not set!\n";
    F->addFnAttr("unsafe-fp-math", "true");
  }
  */

  //errs() << "  Redux remed examining edge(s) from " << *A << " to " << *B
  //       << '\n';

  //Liberty's reduction
  if (reduxdet.isSumReduction(L, A, B, loopCarried, type)) {
    ++numRegDepsRemovedSumRedux;
    LLVM_DEBUG(errs() << "Resolved by liberty sumRedux\n");
    LLVM_DEBUG(errs() << "Removed reg dep between inst " << *A << "  and  " << *B
                 << '\n');
    remedResp.depRes = DepResult::NoDep;
    remedy->liveOutV = B;
    remedy->type = type;
    remedy->reduxSCC = nullptr;
    remedResp.remedy = remedy;
    return remedResp;
  }
  const Instruction *depInst;
  Reduction::Type depType;
  const Instruction *depUpdateInst;
  const CmpInst *cmpInst; //added support for cmpInst, before, only selectInst was supported
  if (reduxdet.isMinMaxReduction(L, A, B, loopCarried, type, &depInst,
                                 depType, &depUpdateInst, &cmpInst)) {
    ++numRegDepsRemovedMinMaxRedux;
    LLVM_DEBUG(errs() << "Resolved by liberty MinMaxRedux\n");
    LLVM_DEBUG(errs() << "Removed reg dep between inst " << *A << "  and  " << *B
                 << '\n');
    remedResp.depRes = DepResult::NoDep;
    remedy->liveOutV = B;
    remedy->type = type;
    remedy->depInst = depInst;
    remedy->depType = depType;
    remedy->depUpdateInst = depUpdateInst;
    remedy->reduxSCC = nullptr;
    remedy->cmpInst = cmpInst;
    remedResp.remedy = remedy;
    return remedResp;
  }

  //noelle's reduction
  /*auto aSCC = loopDepInfo->getSCCManager()->getSCCDAG()->sccOfValue(ncA);
  auto bSCC = loopDepInfo->getSCCManager()->getSCCDAG()->sccOfValue(ncB);
  if (aSCC == bSCC &&
      loopDepInfo->getSCCManager()->getSCCAttrs(aSCC)->canExecuteReducibly()) {
    ++numRegDepsRemovedNoelleRedux;
    LLVM_DEBUG(errs() << "Resolved by noelle Redux\n");
    LLVM_DEBUG(errs() << "Removed reg dep between inst " << *A << "  and  " << *B
                 << '\n');
    remedResp.depRes = DepResult::NoDep;
    remedy->reduxSCC = aSCC;
    if (isa<PHINode>(B))
      remedy->liveOutV = B;

    bool foundAssocCommutBinOp = false;
    for (auto node : make_range(aSCC->begin_nodes(), aSCC->end_nodes())) {
      if (!aSCC->isInternal(node->getT()))
        continue;

      BinaryOperator *binop = dyn_cast<BinaryOperator>(node->getT());

      if (binop) {
        if (Reduction::Type type = Reduction::isAssocAndCommut(binop)) {
          if (foundAssocCommutBinOp && (type != remedy->type)) {
            errs() << "More than 1 accoc & commut binops in redux SCC!!";
            remedResp.depRes = DepResult::Dep;
            return remedResp;
          }
          //assert((!foundAssocCommutBinOp || (type == remedy->type)) &&
          //       "More than 1 accoc & commut binops in redux SCC!!");
          foundAssocCommutBinOp = true;
          remedy->type = type;
          LLVM_DEBUG(errs() << "Binop in redux SCC: " << *binop
                       << " ,and redux type:" << type << '\n');
        }
      }
    }
    if (!foundAssocCommutBinOp) {
      errs() << "Noelle redux SCC does not have a assoc & commut binop!!\n";
      remedResp.depRes = DepResult::Dep;
      return remedResp;
    }
    //assert(foundAssocCommutBinOp &&
    //       "Noelle redux SCC does not have a assoc & commut binop!!");

    remedResp.remedy = remedy;
    return remedResp;
  }*/

  // use Nick's Redux
  /*if (isRegReductionPHI(ncB, ncL)) {
    // B: x0 = phi(initial from outside loop, x1 from backedge)
    // A: x1 = x0 + ..
    // Loop-carried dep removed
    ++numRegDepsRemovedRedux;
    LLVM_DEBUG(errs() << "Resolved by liberty (specpriv but hopefully conservative) redux detection (loop-carried)\n");
    LLVM_DEBUG(errs() << "Removed reg dep between inst " << *A << "  and  " << *B
                 << '\n');
    remedResp.depRes = DepResult::NoDep;
    remedy->liveOutV = B;
    remedy->reduxSCC = nullptr;
    remedResp.remedy = remedy;
    return remedResp;
  }*/

  // already know that instruction A is an operand of instruction B
  RecurrenceDescriptor recdes;

  // since we run loop-simplify
  // before applying the remediators,
  // loops should be in canonical
  // form and have preHeaders. In
  // some cases though, loopSimplify
  // is unable to canonicalize some
  // loops. Thus we need to check
  // first

  if (L->getLoopPreheader()) {
    if (PHINode *PhiB = dyn_cast<PHINode>(ncB)) {
      if (RecurrenceDescriptor::isReductionPHI(PhiB, ncL,
                                               recdes)) {
        // B: x0 = phi(initial from outside loop, x1 from backedge)
        // A: x1 = x0 + ..
        // Loop-carried dep removed
        ++numRegDepsRemovedLLVMRedux;
        LLVM_DEBUG(errs() << "Resolved by llvm redux detection (loop-carried)\n");
        LLVM_DEBUG(errs() << "Removed reg dep between inst " << *A << "  and  " << *B
                     << '\n');
        remedResp.depRes = DepResult::NoDep;
        remedy->liveOutV = B;
        remedy->reduxSCC = nullptr;
        remedResp.remedy = remedy;
        return remedResp;
      }
    }
  }

  if (isConditionalReductionPHI(B, L)) {
    ++numCondRegDepsRemoved;
    LLVM_DEBUG(errs() << "Resolved by cond redux detection\n");
    LLVM_DEBUG(errs() << "Removed reg dep between inst " << *A << "  and  " << *B
                 << '\n');
    remedResp.depRes = DepResult::NoDep;
    remedy->liveOutV = B;
    remedy->reduxSCC = nullptr;
    remedResp.remedy = remedy;
    return remedResp;
  }

  //errs() << "Redux remed unable to resolve this dep\n";
  remedResp.remedy = remedy;
  return remedResp;
}

Remediator::RemedResp ReduxRemediator::memdep(const Instruction *A,
                                              const Instruction *B,
                                              bool LoopCarried,
                                              DataDepType dataDepTy,
                                              const Loop *L) {

  Remediator::RemedResp remedResp;
  // conservative answer
  remedResp.depRes = DepResult::Dep;
  auto remedy = std::make_shared<ReduxRemedy>();
  //remedy->cost = DEFAULT_REDUX_REMED_COST;
  remedy->cost = 0;

  if (!LoopCarried)
    return remedResp;

  if (isMemReduction(A)) {
    ++numMemDepsRemovedRedux;
    LLVM_DEBUG(errs() << "Removed mem dep between inst " << *A << "  and  " << *B
                 << '\n');
    remedResp.depRes = DepResult::NoDep;
    remedy->liveOutV = A;
    remedy->reduxSCC = nullptr;
    remedResp.remedy = remedy;
    return remedResp;
  }

  if (isMemReduction(B)) {
    ++numMemDepsRemovedRedux;
    LLVM_DEBUG(errs() << "Removed mem dep between inst " << *A << "  and  " << *B
                 << '\n');
    remedResp.depRes = DepResult::NoDep;
    remedy->liveOutV = B;
    remedy->reduxSCC = nullptr;
    remedResp.remedy = remedy;
    return remedResp;
  }

  remedResp.remedy = remedy;
  return remedResp;
}

} // namespace liberty
