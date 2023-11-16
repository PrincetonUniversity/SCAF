/*
 * Copyright 2016 - 2021  Angelo Matni, Simone Campanoni
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 of this software and associated documentation files (the "Software"), to deal
 in the Software without restriction, including without limitation the rights to
 use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies
 of the Software, and to permit persons to whom the Software is furnished to do
 so, subject to the following conditions:

 * The above copyright notice and this permission notice shall be included in
 all copies or substantial portions of the Software.

 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.
 * IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM,
 DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR
 OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE
 OR OTHER DEALINGS IN THE SOFTWARE.
 */
#pragma once
#include "DGBase.h"
#include "llvm/IR/Value.h"

namespace liberty::pdgtester {

/*
 * Program Dependence Graph.
 */
class PDG : public DG<Value> {
public:
  /*
   * Constructor:
   * Add all instructions included in the module M as nodes to the PDG.
   */
  //PDG(Module &M) {
  //}

  /*
   * Constructor:
   * Add all instructions included in the function F as nodes to the PDG.
   */
  //PDG(Function &F) {}

  /*
   * Constructor:
   * Add all instructions included in the loop only.
   */
  PDG(Loop *loop) {
    for(auto bbi = loop->block_begin(); bbi != loop->block_end(); ++bbi){
      for(auto &I: **bbi) {
        this->addNode(cast<Value>(&I), true);
      }
    }
  }

  /*
   * Fetch dependences between two values/instructions.
   */
  //std::unordered_set<DGEdge<Value> *> getDependences(Value *v1, Value *v2);

  /*
   * Add the edge from "from" to "to" to the PDG.
   */
  DGEdge<Value> *addEdge(Value *from, Value *to) {
    return this->DG<Value>::addEdge(from, to);
  }

  /*
   * Destructor
   */
  ~PDG() {
    for(auto *edge: allEdges) {
      if(edge)
        delete edge;
    }
    for(auto *node : allNodes) {
      if(node)
        delete node;
    }
  }
};

} // namespace llvm::noelle
