
#ifndef LLVM_LIBERTY_ACYCLICAA
#define LLVM_LIBERTY_ACYCLICAA

#include "llvm/ADT/DenseSet.h"
#include "llvm/Analysis/TargetLibraryInfo.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/Module.h"
#include "llvm/Pass.h"

#include "scaf/MemoryAnalysisModules/ClassicLoopAA.h"

namespace liberty {
using namespace llvm;
using namespace arcana::noelle;

/* A pass which tries to identify acyclic recursive types.
 */
class AcyclicAA : public ModulePass, public ClassicLoopAA {

  typedef DenseSet<Type *> TypeSet;
  TypeSet acyclic;
  typedef std::vector<Type *> Types;

  void accumulateRecursiveTypes(Types &recTysOut) const;
  void accumulateRecursiveTypes(Function &f, TypeSet &visited,
                                Types &recTysOut) const;

  typedef std::set<const Value *> SmallValueSet;
  bool isChildOfTransitive(const Value *v1, const Value *v2,
                           TemporalRelation rel, const Loop *L,
                           SmallValueSet &noInfiniteLoops) const;
  bool isChildOfTransitive(const Value *v1, const Value *v2,
                           TemporalRelation rel, const Loop *L) const;

  Module *currentModule;

public:
  static char ID;
  AcyclicAA() : ModulePass(ID), ClassicLoopAA() {}

  void getAnalysisUsage(AnalysisUsage &au) const;

  bool runOnModule(Module &);

  StringRef getPassName() const {
    return "Identify acyclic recursive data structures (in the simplest of all "
           "possible senses)";
  }

  AliasResult aliasCheck(const Pointer &P1, TemporalRelation rel,
                         const Pointer &P2, const Loop *L, Remedies &R,
                         DesiredAliasResult dAliasRes = DNoOrMustAlias);

  /// getAdjustedAnalysisPointer - This method is used when a pass implements
  /// an analysis interface through multiple inheritance.  If needed, it
  /// should override this to adjust the this pointer as needed for the
  /// specified pass info.
  virtual void *getAdjustedAnalysisPointer(AnalysisID PI) {
    if (PI == &LoopAA::ID)
      return (LoopAA *)this;
    return this;
  }

  StringRef getLoopAAName() const { return "AcyclicAA"; }
};
} // namespace liberty

#endif // LLVM_LIBERTY_ACYCLICAA

