#define DEBUG_TYPE "lamp-oracle-aa"

#define LAMP_COLLECTS_OUTPUT_DEPENDENCES  (0)

#include "scaf/MemoryAnalysisModules/Introspection.h"
#include "scaf/SpeculationModules/LAMP/LampOracleAA.h"
#include "scaf/SpeculationModules/Remediator.h"

#include "llvm/ADT/Statistic.h"
#include "llvm/IR/IntrinsicInst.h"

#define DEFAULT_LAMP_REMED_COST 1500

namespace liberty
{
  using namespace llvm;
  using namespace arcana::noelle;

  STATISTIC(numQueries,       "Num queries");
  STATISTIC(numEligible,      "Num eligible queries");
  STATISTIC(numNoForwardFlow, "Num forward no-flow results");
  STATISTIC(numNoReverseFlow, "Num reverse no-flow results");

  static cl::opt<unsigned> Threshhold(
    "lamp-oracle-threshhold", cl::init(0),
    cl::NotHidden,
    cl::desc("Maximum number of observed flows to report NoModRef"));

  void getSubLoopList(const Loop* L, const Instruction *A, const Instruction *B, std::vector<Loop*> &subLoopList) {
    for(auto subloop : L->getSubLoops()) {
      subLoopList.push_back(subloop);
      if(!(subloop->getSubLoops().empty())) {
        getSubLoopList(subloop, A, B, subLoopList);
      }
    }
    return;
  }
      //if((lamp->numObsInterIterDep(subloop->getHeader(), B, A) > Threshhold) ||
      //    (lamp->numObsIntraIterDep(subloop->getHeader(), B, A) > Threshhold))
      //  return Dep;

  LoopAA::AliasResult LampOracle::alias(const Value *ptrA, unsigned sizeA,
                                        TemporalRelation rel, const Value *ptrB,
                                        unsigned sizeB, const Loop *L,
                                        Remedies &R,
                                        DesiredAliasResult dAliasRes) {
    return LoopAA::alias(ptrA, sizeA, rel, ptrB, sizeB, L, R, dAliasRes);
  }

  bool LampRemedy::compare(const Remedy_ptr rhs) const {
    std::shared_ptr<LampRemedy> lamp_rhs = 
      std::static_pointer_cast<LampRemedy>(rhs);

    if(this->srcI == lamp_rhs->srcI) {
      return this->dstI < lamp_rhs->dstI;
    }
    return this->srcI < lamp_rhs->srcI;
  }

  void LampRemedy::setCost(PerformanceEstimator *perf) {
    assert(this->srcI && this->dstI && "no srcI or dstI in LAMP remedy???");

    auto addCost = [this, &perf](const Instruction *memI) {
      double validation_weight = 0.0000738;
      if(isa<LoadInst>(memI))
        validation_weight = 0.0000276;
      this->cost += perf->weight_with_gravity(memI, validation_weight);
    };

    addCost(this->srcI);
    addCost(this->dstI);
  }

  LoopAA::ModRefResult LampOracle::modref(
    const Instruction *A,
    TemporalRelation rel,
    const Value *ptrB, unsigned sizeB,
    const Loop *L, Remedies &R)
  {
    return LoopAA::modref(A,rel,ptrB,sizeB,L,R);
  }

  //FIXME: declared in both SlampOracle
  static bool isMemIntrinsic(const Instruction *inst)
  {
    return isa< MemIntrinsic >(inst);
  }


  //FIXME: declared in both SlampOracle
  static bool intrinsicMayRead(const Instruction *inst)
  {
    ImmutableCallSite cs(inst);
    StringRef  name = cs.getCalledFunction()->getName();
    if( name == "llvm.memset.p0i8.i32"
    ||  name == "llvm.memset.p0i8.i64" )
      return false;

    return true;
  }

  Remediator::RemedResp LampOracle::memdep(const Instruction *A, const Instruction *B,
                                bool loopCarried, DataDepType dataDepTy,
                                const Loop *L) {
    RemedResp resp;
    resp.depRes = Dep;

    if(!L)
      return resp;

    if((loopCarried && lamp->numObsInterIterDep(L->getHeader(), B, A) <= Threshhold)
        || (!loopCarried && lamp->numObsIntraIterDep(L->getHeader(), B, A) <=Threshhold)) {
      //errs() << "LAMP in the if statement: " << *A << " to " << *B << "\n";
      resp.depRes = NoDep;

      if(!loopCarried) {
        std::vector<Loop*> subLoopList;
        getSubLoopList(L, A, B, subLoopList);
        for(auto subloop: subLoopList) {
          if((lamp->numObsInterIterDep(subloop->getHeader(), B, A) > Threshhold) ||
              (lamp->numObsIntraIterDep(subloop->getHeader(), B, A) > Threshhold)) {
            //errs() << "LAMP dep in inner loop\n";
            resp.depRes = Dep;
            break;
          }
        }
        subLoopList.clear();
      }
      auto remedy = std::make_shared<LampRemedy>();
      remedy->srcI = A;
      remedy->dstI = B;
      if(perf) {
        remedy->setCost(perf);
      } else {
        remedy->cost = DEFAULT_LAMP_REMED_COST;
      }
      resp.remedy = remedy;
      return resp;
    }
    return resp;

  }


  LoopAA::ModRefResult LampOracle::modref(
    const Instruction *A,
    TemporalRelation rel,
    const Instruction *B,
    const Loop *L,
    Remedies &R)
  {
    ++numQueries;

    // Lamp profile data is loop sensitive.
    if( !L )
      // Inapplicable
      return LoopAA::modref(A,rel,B,L,R);

    INTROSPECT(ENTER(A,rel,B,L));

    ModRefResult result = ModRef;

    // Loop carried forward queries, or
    // Same queries.
    if( rel == Before || rel == Same )
    {
      // Lamp profile data is only collected for
      // loads and stores; not callsites.
      // Lamp collects FLOW and OUTPUT info, but
      // not ANTI or FALSE dependence data.
      // Thus, for Before/Same queries, we are looking
      // for Store -> Load/Store
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

      else
      {
        // Callsites, etc: inapplicable
        result = LoopAA::modref(A,rel,B,L,R);
        INTROSPECT(EXIT(A,rel,B,L,result));
        return result;
      }

      // Again, only Store vs (Load/Store)
      if( isa<LoadInst>(B) )
      {
        // okay
      }
      else if( isMemIntrinsic(B) && intrinsicMayRead(B) )
      {
        // okay
      }
      else
      {
        if( ! (LAMP_COLLECTS_OUTPUT_DEPENDENCES && isa<StoreInst>(B)) )
        {
          // inapplicable
          result = ModRefResult(result & LoopAA::modref(A,rel,B,L,R) );
          INTROSPECT(EXIT(A,rel,B,L,result));
          return result;
        }
      }

      if( rel == Before )
      {
        ++numEligible;
        // Query profile data
        if( lamp->numObsInterIterDep(L->getHeader(), B, A ) <= Threshhold )
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
        if( lamp->numObsIntraIterDep(L->getHeader(), B, A ) <= Threshhold )
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
      // Lamp profile data is only collected for
      // loads and stores; not callsites.
      // Lamp collects FLOW and OUTPUT info, but
      // not ANTI or FALSE dependence data.
      // Thus, for After queries, we are looking
      // for Load/Store -> Store
      if( isa<LoadInst>(A) )
        // Anti or False: inapplicable
        result = Ref;

      else if( isMemIntrinsic(A) && intrinsicMayRead(A) )
        result = ModRef;

      else if( LAMP_COLLECTS_OUTPUT_DEPENDENCES && isa<StoreInst>(A) )
        // Stores don't ref
        result = Mod;

      else
      {
        // Callsites, etc: inapplicable
        result = LoopAA::modref(A,rel,B,L,R);
        INTROSPECT(EXIT(A,rel,B,L,result));
        return result;
      }


      // Again, only (Load/Store) vs Store
      if( isa<StoreInst>(B) )
      {
        // good
      }
      else if( isMemIntrinsic(B) )
      {
        // good
      }
      else
      {
        // inapplicable
        result = ModRefResult(result & LoopAA::modref(A,rel,B,L,R));
        INTROSPECT(EXIT(A,rel,B,L,result));
        return result;
      }

      ++numEligible;
      // Query profile data.
      if( lamp->numObsInterIterDep(L->getHeader(), A, B ) <= Threshhold )
      {
        // No flow.
        if( isa<LoadInst>(B) )
          result = ModRefResult(result & ~Ref);

        else if( isa<StoreInst>(B) )
          result = ModRefResult(result & ~Mod);

        ++numNoReverseFlow;
      }
    }

    if( result != NoModRef )
      // Chain.
      result = ModRefResult(result & LoopAA::modref(A,rel,B,L,R) );

    INTROSPECT(EXIT(A,rel,B,L,result));
    return result;
  }

}


