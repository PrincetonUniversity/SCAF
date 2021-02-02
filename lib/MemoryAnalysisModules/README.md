## Analyses and File Mapping

Implemented:

- [Acyclic](#acyclic): AcyclicAA.cpp/AcyclicAA.h
- [Non-Captured Fields](#non-captured-fields): NoEscapeFieldsAA.cpp
- [Sane Typing](#sane-typing): TypeAA.cpp
- [Scalar Evolution](#scalar-evolution): SCEVAA.cpp
- [Array of Structures](#array-of-structures): ArrayOfStructures.cpp
- [Unique Access Paths](#unique-access-paths): UniquePaths.cpp
- [Non-Captured Source](#non-captured-source): NoCaptureSrcAA.cpp
- [Non-Captured Global](#non-captured-global): NoCaptureGlobalAA.cpp
- [Global Malloc](#global-malloc): GlobalMallocAA.cpp
- [Callsite Depth-Combinator](#callsite-depth-combinator): CallsiteDepthCombinator.cpp
- [Kill Flow](#kill-flow): KillFlow.cpp
- [Pure and Semi-Local Functions](#pure-and-semi-local-functions): PureFunAA.cpp/SemiLocalFunAA.cpp/SemiLocalFun.h/LocalFun.h
- [Phi-Maze](#phi-maze): PHIMazeAA.cpp
- [Basic Loop](#basic-loop): BasicLoopAA.cpp
- [Auto Restrict](#auto-restrict): AutoRestrictAA.cpp
- [Disjoint Field](#disjoint-field): DisjointFieldAA.cpp
- [Field Malloc](#field-malloc): FieldMallocAA.cpp
- [SVF Results](#svf-results): SVFResults.cpp

Not Implemented: 

- [SMTAA](#smtaa):

Unclassified Files:
- AnalysisTimeout.cpp
- CallsiteBreadthCombinator.cpp
- CallsiteSearch.cpp
- ClassicLoopAA.cpp
- CMakeLists.txt
- FindSource.cpp
- FormatStrings.h
- GetCallers.cpp
- IntrinsicAA.cpp
- Introspection.cpp
- LLVMAAResults.cpp
- LoopAA.cpp
- LoopVariantAllocation.cpp
- LoopVariantAllocation.h
- NoCaptureFcn.cpp
- NoMemFun.h
- ReadOnlyFormal.h
- RefineCFG.cpp
- RefineCFG.h
- SimpleAA.cpp
- StdInOutErr.cpp
- StdInOutErr.h
- SubloopCombinator.cpp
- TraceData.cpp
- WriteOnlyFormal.h

## Detailed Descriptions
### Acyclic
- Implementation: class AcyclicAA
- Tactic: disprove aliasing; type sanity; reachability; shape analysis.
- Initialization costs: linear in size of input program.
- Cost / Query: linear in size of a function.
- Foreign Premise Queries / Query: none.
- Description: It identifies acyclic data structures as cases where recursive,
    non-captured fields are updated in a restricted manner.

### Non-Captured Fields

- Implementation: classes NonCapturedFieldsAnalysis and NoEscape-
FieldsAA
- Tactic: disprove aliasing; type sanity; reachability. 
- Initialization costs: linear in size of program.
- Cost / Query: constant.
- Foreign Premise Queries / Query: 0 or 1.
- Description:

### Sane Typing
- Implementation: classes TypeSanityAnalysis and TypeAA
- Tactic: disprove aliasing; type sanity.
- Initialization costs: linear in size of module.
- Cost / Query: constant.
- Foreign Premise Queries / Query: 0 or 1.

