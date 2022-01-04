This directory contains speculation modules described in the SCAF paper and
some extra ones.  These modules propose speculative assertions and collaborate
with other modules to remove dependence speculatively.  The removed dependences
are marked as removable and annotated with corresponding remedies.  These
remedies need to have corresponding validation parts in the CodeGen if a sound
compilation is intended. (Otherwise might be suitable for approximation
computing etc).

## Profilers

- Edge Profiler: identifies biased branches
- Value-prediction Profiler: detects predictable loads
- Points-to Profiler: produces a points-to map, allowing detection of
  underlying objects for every memory access
- Object-lifetime Profiler: detects short-lived memory objects
- Loop-Aware Memory Profiler\*: identifies the absense of memory dependence in
  profiles

## Speculation Modules

- Pointer-Residue Speculation
- Points-to Speculation
- Control Speculation
- Value Prediction
- Read-only Speculation
- Short-lived Speculation
- Memory Speculation\*

\* Not a part of the original SCAF paper
