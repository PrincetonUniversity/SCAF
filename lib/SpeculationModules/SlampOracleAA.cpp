#include <memory>
#define DEBUG_TYPE "slamp-oracle-aa"

#include "scaf/SpeculationModules/SlampOracleAA.h"
#include "scaf/SpeculationModules/Remediator.h"

#include "llvm/ADT/Statistic.h"
#include "llvm/IR/IntrinsicInst.h"

#define DEFAULT_SLAMP_REMED_COST 1500

namespace liberty
{

using namespace llvm;

STATISTIC(numQueries,       "Num queries");
STATISTIC(numEligible,      "Num eligible queries");
STATISTIC(numNoForwardFlow, "Num forward no-flow results");
STATISTIC(numNoReverseFlow, "Num reverse no-flow results");

static cl::opt<unsigned> Threshold(
  "slamp-oracle-threshold", cl::init(0),
  cl::NotHidden,
  cl::desc("Maximum number of observed flows to report NoModRef"));

LoopAA::AliasResult SlampOracleAA::alias(const Value *ptrA, unsigned sizeA,
                                       TemporalRelation rel, const Value *ptrB,
                                       unsigned sizeB, const Loop *L,
                                       Remedies &R,
                                       DesiredAliasResult dAliasRes) {
  return LoopAA::alias(ptrA, sizeA, rel, ptrB, sizeB, L, R, dAliasRes);
}

// FIXME: shouldn't remedies be comparied against other remedies?
bool SlampRemedy::compare(const Remedy_ptr rhs) const {
  std::shared_ptr<SlampRemedy> slamp_rhs =
    std::static_pointer_cast<SlampRemedy>(rhs);

  if (this->srcI == slamp_rhs->srcI) {
    return this->dstI < slamp_rhs->dstI;
  }
  return this->srcI < slamp_rhs->srcI;
}

void SlampRemedy::setCost(PerformanceEstimator *perf) {
  assert(this->srcI && this->dstI && "no srcI or dstI in SLAMP remedy???");

  auto addCost = [this, &perf](const Instruction *memI) {
    // FIXME: what is this hardcoded constant doing
    double validation_weight = 0.0000738;
    if (isa<LoadInst>(memI))
      validation_weight = 0.0000276;
    this->cost += perf->weight_with_gravity(memI, validation_weight);
  };

  // multiply validation cost time with number of estimated invocations
  addCost(this->srcI);
  addCost(this->dstI);
}

LoopAA::ModRefResult SlampOracleAA::modref(
  const Instruction *A,
  TemporalRelation rel,
  const Value *ptrB, unsigned sizeB,
  const Loop *L, Remedies &R)
{
  //std::string space(getDepth()+1, ' ');
  //errs() << space << "si\n";
  return LoopAA::modref(A,rel,ptrB,sizeB,L,R);
}

bool isMemIntrinsic(const Instruction *inst)
{
  return isa< MemIntrinsic >(inst);
}

bool intrinsicMayRead(const Instruction *inst)
{
  ImmutableCallSite cs(inst);
  StringRef  name = cs.getCalledFunction()->getName();
  if( name == "llvm.memset.p0i8.i32"
  ||  name == "llvm.memset.p0i8.i64" )
    return false;

  return true;
}

Remediator::RemedResp SlampOracleAA::memdep(const Instruction *A, const Instruction *B,
                           bool loopCarried, DataDepType dataDepTy,
                           const Loop *L) {
  RemedResp resp;
  resp.depRes = Dep;

  // SLAMP only targets the loops it recognizes and only RAW deps
  if (!L || !slamp->isTargetLoop(L) || dataDepTy!=RAW) {
    return resp;
  }

  if ((loopCarried && slamp->numObsInterIterDep(L->getHeader(), B, A) <= Threshold)
    || (!loopCarried && slamp->numObsIntraIterDep(L->getHeader(), B, A) <= Threshold)) {
    resp.depRes = NoDep;

    auto remedy = make_shared<SlampRemedy>();
    remedy->srcI = A;
    remedy->dstI = B;
    if (perf) {
      remedy->setCost(perf);
    } else {
      remedy->cost = DEFAULT_SLAMP_REMED_COST;
    }
    resp.remedy = remedy;

    return resp;
  }

  return resp;
}

LoopAA::ModRefResult SlampOracleAA::modref(
  const Instruction *A,
  TemporalRelation rel,
  const Instruction *B,
  const Loop *L, Remedies &R)
{
  ++numQueries;

  // Slamp profile data is loop sensitive.
  if( !L || !slamp->isTargetLoop(L) )
  {
    // Inapplicable
    //std::string space(getDepth()+1, ' ');
    //errs() << space << "si\n";
    return LoopAA::modref(A,rel,B,L,R);
  }

  // both instructions should be included in the target loop
  bool includeA = false;
  bool includeB = false;

  for (Loop::block_iterator bi = L->block_begin() ; bi != L->block_end() ; bi++)
  {
    if ( *bi == A->getParent() )
      includeA = true;
    if ( *bi == B->getParent() )
      includeB = true;
  }

  if ( !includeA || !includeB )
  {
    // Inapplicable
    //std::string space(getDepth()+1, ' ');
    //errs() << space << "si\n";
    return LoopAA::modref(A,rel,B,L,R);
  }

  ModRefResult result = ModRef;

  // Loop carried forward queries, or
  // Same queries.
  if( rel == Before || rel == Same )
  {
    // Slamp profile data is colected for loads, stores, and callistes.
    // Slamp only collect FLOW info.
    // Thus, for Before/Same queries, we are looking
    // for Store/CallSite -> Load/CallSite

    if( isa<StoreInst>(A) )
      // Stores don't ref
      result = Mod;

    else if( isMemIntrinsic(A) )
    {
      if( intrinsicMayRead(A) )
        result = ModRef;
      else
        result = Mod;
    }

    else if( isa<CallBase>(A) )
    {
      result = ModRef;
    }
    else
    {
      // inapplicable
      //std::string space(getDepth()+1, ' ');
      //errs() << space << "si\n";
      result = LoopAA::modref(A,rel,B,L,R);
      return result;
    }

    if( isa<LoadInst>(B) )
    {
      // okay
    }
    else if( isMemIntrinsic(B) && intrinsicMayRead(B) )
    {
      // okay
    }
    else if ( isa<CallBase>(B) )
    {
      // okay
    }
    else
    {
      // inapplicable, as Slamp does not collect output dependence
      //std::string space(getDepth()+1, ' ');
      //errs() << space << "si\n";
      result = ModRefResult(result & LoopAA::modref(A,rel,B,L,R) );
      return result;
    }

    if( rel == Before )
    {
      ++numEligible;
      // Query profile data
      if( slamp->numObsInterIterDep(L->getHeader(), B, A) <= Threshold )
      {
        // No flow.
        result = ModRefResult(result & ~Mod);
        ++numNoForwardFlow;
      }
    }

    else if( rel == Same )
    {
      ++numEligible;
      // Query profile data
      if( slamp->numObsIntraIterDep(L->getHeader(), B, A) <= Threshold )
      {
        // No flow
        result = ModRefResult(result & ~Mod);
        ++numNoForwardFlow;
      }
    }
  }

  // Loop carried reverse queries.
  else if( rel == After )
  {
    // Slamp profile data is colected for loads, stores, and callistes.
    // Slamp only collect FLOW info.
    // Thus, for After queries, we are looking
    // for Store/CallSite -> Load/CallSite

    // Lamp profile data is only collected for
    // loads and stores; not callsites.
    // Lamp collects FLOW and OUTPUT info, but
    // not ANTI or FALSE dependence data.
    // Thus, for After queries, we are looking
    // for Load/CallSite -> Store/CallSite

    if( isa<LoadInst>(A) )
      // Anti or False: inapplicable
      result = Ref;

    else if( isMemIntrinsic(A) && intrinsicMayRead(A) )
      result = ModRef;

    else if( isa<CallBase>(A) )
      result = ModRef;

    else
    {
      // inapplicable
      //std::string space(getDepth()+1, ' ');
      //errs() << space << "si\n";
      result = LoopAA::modref(A,rel,B,L,R);
      return result;
    }

    // Again, only (Load/Callsite) vs (Store/CallSite)
    if( isa<StoreInst>(B) )
    {
      // good
    }
    else if( isMemIntrinsic(B) )
    {
      // good
    }
    else if ( isa<CallBase>(B) )
    {
      // good
    }
    else
    {
      // inapplicable
      //std::string space(getDepth()+1, ' ');
      //errs() << space << "si\n";
      result = ModRefResult(result & LoopAA::modref(A,rel,B,L,R));
      return result;
    }

    ++numEligible;
    // Query profile data.
    if( slamp->numObsInterIterDep(L->getHeader(), A, B) <= Threshold )
    {
      result = ModRefResult(result & ~Mod);
      ++numNoReverseFlow;
    }
  }

  if( result != NoModRef )
  {
    //std::string space(getDepth()+1, ' ');
    //errs() << space << "sr " << result << "\n";
    // Chain.
    LoopAA::ModRefResult chainresult = LoopAA::modref(A,rel,B,L,R);

    /*
     * // FIXME: Very intersting conflict check
     * if ( result > ( result & chainresult ) )
     * {
     *   errs() << "result " << result << "\n";
     *   errs() << "chainresult " << chainresult << "\n";
     *   errs() << A->getParent()->getParent()->getName() << "::" << A->getParent()->getName();
     *   A->dump();
     *   errs() << B->getParent()->getParent()->getName() << "::" << B->getParent()->getName();
     *   B->dump();
     *   errs() << "rel: " << rel << "\n";
     *   assert( false );
     * }
     * result = ModRefResult(result & LoopAA::modref(A,rel,B,L,R) );
     */
    result = ModRefResult(result & chainresult);
  }

  return result;
}

}
