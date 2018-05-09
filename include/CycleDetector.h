#ifndef ANDERSEN_CYCLEDETECTOR_H
#define ANDERSEN_CYCLEDETECTOR_H

#include "GraphTraits.h"

#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/DenseSet.h"

#include <deque>
#include <stack>

// An abstract base class that offers the functionality of detecting SCC in a
// graph Any concreate class that does cycle detection should inherit from this
// class, specifiy the GraphType, and implement all the abstract virtual
// functions
template <class GraphType> class CycleDetector {
public:
  typedef AndersGraphTraits<GraphType> GraphTraits;
  typedef typename GraphTraits::NodeType NodeType;
  typedef typename GraphTraits::NodeIterator node_iterator;
  typedef typename GraphTraits::ChildIterator child_iterator;

private:
  // The SCC stack
  std::stack<const NodeType *> sccStack;
  // Map from NodeIndex to DFS number. Nodes that are not in the map are never
  // visited
  llvm::DenseMap<const NodeType *, unsigned> dfsNum;
  // The "inComponent" array in Nutilla's improved SCC algorithm
  llvm::DenseSet<const NodeType *> inComponent;
  // DFS timestamp
  unsigned timestamp;

  // visiting each node and perform some task
  void visit(NodeType *node) {
    unsigned myTimeStamp = timestamp++;
    assert(!dfsNum.count(node) && "Revisit the same node again?");
    dfsNum[node] = myTimeStamp;

    // Traverse succecessor edges
    for (auto childItr = GraphTraits::child_begin(node),
              childIte = GraphTraits::child_end(node);
         childItr != childIte; ++childItr) {
      NodeType *succRep = getRep(*childItr);
      if (!dfsNum.count(succRep))
        visit(succRep);

      if (!inComponent.count(succRep) && dfsNum[node] > dfsNum[succRep])
        dfsNum[node] = dfsNum[succRep];
    }

    // See if we have any cycle detected
    if (myTimeStamp != dfsNum[node]) {
      // If not, push the sccStack and go on
      sccStack.push(node);
      return;
    }

    // Cycle detected
    inComponent.insert(node);
    while (!sccStack.empty()) {
      const NodeType *cycleNode = sccStack.top();
      if (dfsNum[cycleNode] < myTimeStamp)
        break;

      processNodeOnCycle(cycleNode, node);
      inComponent.insert(cycleNode);
      sccStack.pop();
    }

    processCycleRepNode(node);
  }

protected:
  // Nodes may get merged during the analysis. This function returns the merge
  // target (if the node is merged into another node) or the node itself (if the
  // nodes has not been merged into another node)
  virtual NodeType *getRep(NodeIndex node) = 0;
  // Specify how to process the non-rep nodes if a cycle is found
  virtual void processNodeOnCycle(const NodeType *node,
                                  const NodeType *repNode) = 0;
  // Specify how to process the rep nodes if a cycle is found
  virtual void processCycleRepNode(const NodeType *node) = 0;

  // Running the cycle detection algorithm on a given graph G
  void runOnGraph(GraphType *graph) {
    assert(sccStack.empty() && "sccStack is not empty before cycle detection!");
    assert(dfsNum.empty() && "dfsNum is not empty before cycle detection!");
    assert(inComponent.empty() &&
           "inComponent is not empty before cycle detection!");

    for (auto itr = GraphTraits::node_begin(graph),
              ite = GraphTraits::node_end(graph);
         itr != ite; ++itr) {
      NodeType *repNode = getRep(itr->getNodeIndex());
      if (!dfsNum.count(repNode))
        visit(repNode);
    }

    assert(sccStack.empty() && "sccStack not empty after cycle detection!");
  }

  // Running the cycle detection algorithm on a given graph node. This function
  // is used when walking through the entire graph is not the desirable
  // behavior.
  void runOnNode(NodeIndex node) {
    assert(sccStack.empty() && "sccStack is not empty before cycle detection!");

    NodeType *repNode = getRep(node);
    if (!dfsNum.count(repNode))
      visit(repNode);

    assert(sccStack.empty() && "sccStack not empty after cycle detection!");
  }

  void releaseSCCMemory() {
    dfsNum.clear();
    inComponent.clear();
  }

public:
  CycleDetector() : timestamp(0) {}

  // The public interface of running the detector
  virtual void run() = 0;
};

#endif
