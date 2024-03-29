#!/usr/bin/env bash

#SCAF_LIBS_DIR="/u/ziyangx/SCAF/build/install/lib"
SCAF_LIBS_DIR="/u/ziyangx/SCAF/build-release/install/lib"
# if [[ x"$SCAF_LIBS_DIR" == x"" ]]
# then
#     echo SCAF_LIBS_DIR not found, please export it
#     exit
# fi

if [ ! -f $SCAF_LIBS_DIR/libSpeculationModules.so ];
then
    echo Speculation Modules not found, please reexport $SCAF_LIBS_DIR
    exit
fi

SCAF_BASE_LIBS="-load $SCAF_LIBS_DIR/libSCAFUtilities.so -load $SCAF_LIBS_DIR/libMemoryAnalysisModules.so"
SCAF_SPEC_LIBS="-load $SCAF_LIBS_DIR/libLoopProf.so \
    -load $SCAF_LIBS_DIR/libLAMPLoad.so \
    -load $SCAF_LIBS_DIR/libPointsToProfiler.so \
    -load $SCAF_LIBS_DIR/libSpeculationModules.so"

NOELLE_LIBS="-load $NOELLE_LIBS_DIR/CallGraph.so\
    -load $NOELLE_LIBS_DIR/libSvf.so \
    -load $NOELLE_LIBS_DIR/libCudd.so -stat=false \
    -load $NOELLE_LIBS_DIR/AllocAA.so \
    -load $NOELLE_LIBS_DIR/TalkDown.so \
    -load $NOELLE_LIBS_DIR/CallGraph.so \
    -load $NOELLE_LIBS_DIR/PDGAnalysis.so \
    -load $NOELLE_LIBS_DIR/Architecture.so \
    -load $NOELLE_LIBS_DIR/BasicUtilities.so \
    -load $NOELLE_LIBS_DIR/Task.so \
    -load $NOELLE_LIBS_DIR/DataFlow.so \
    -load $NOELLE_LIBS_DIR/HotProfiler.so \
    -load $NOELLE_LIBS_DIR/LoopStructure.so \
    -load $NOELLE_LIBS_DIR/Loops.so \
    -load $NOELLE_LIBS_DIR/Scheduler.so \
    -load $NOELLE_LIBS_DIR/OutlinerPass.so \
    -load $NOELLE_LIBS_DIR/Noelle.so"

AA_PASSES="-basic-loop-aa -scev-loop-aa -auto-restrict-aa -intrinsic-aa \
    -global-malloc-aa -pure-fun-aa -semi-local-fun-aa -phi-maze-aa \
    -no-capture-global-aa -no-capture-src-aa -type-aa -no-escape-fields-aa \
    -acyclic-aa -disjoint-fields-aa -field-malloc-aa -loop-variant-allocation-aa \
    -std-in-out-err-aa -array-of-structures-aa -kill-flow-aa \
    -callsite-depth-combinator-aa -unique-access-paths-aa -llvm-aa-results \
    -basicaa -globals-aa -cfl-steens-aa -tbaa -scev-aa -cfl-anders-aa \
    -objc-arc-aa -scoped-noalias -veto -nander"

DEBUG_PASSES="-debug-pass=Arguments"

ENABLE_EDGE="-enable-edgeprof"
ENABLE_LAMP="-enable-lamp -lamp-assert"
ENABLE_SPECPRIV="-enable-specpriv"

BENCHMARK="benchmark.bc"

ENABLES=""
#ENABLES="$ENABLE_EDGE $ENABLE_LAMP $ENABLE_SPECPRIV"

CMD="opt $SCAF_BASE_LIBS $NOELLE_LIBS $SCAF_SPEC_LIBS $AA_PASSES $ENABLES $DEBUG_PASSES $BENCHMARK -pdgbuilder $1 -dump-pdg -disable-output"
echo $CMD
$CMD
