#include "Andersen.h"

#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/DenseSet.h"
#include "llvm/ADT/SparseBitVector.h"

#include <deque>

using namespace llvm;

// Optimize the constraints by performing offline variable substitution
void Andersen::optimizeConstraints()
{
	// First, let's do HVN
	hvn();
}

// During variable substitution, we create unknowns to represent the unknown value that is a dereference of a variable.  These nodes are known as "ref" nodes (since they represent the value of dereferences)
// Return the node index of the "ref node" (used to represent *n) of n. Only addr-taken vars can be ref-ed
// We won't actually create that ref node. We cannot use the NodeIndex of that refNode to index into nodeFactory
NodeIndex Andersen::getRefNodeIndex(NodeIndex n) const
{
	assert(nodeFactory.isObjectNode(n) && "REF node of a top-level var does not make sense!");
	return n + nodeFactory.getNumNodes();
}

// Return the node index of the "adr node" (used to represent &n) of n. 
// We won't actually create that adr node. We cannot use the NodeIndex of that adrNode to index into nodeFactory
NodeIndex Andersen::getAdrNodeIndex(NodeIndex n) const
{
	return n + 2 * nodeFactory.getNumNodes();
}

// The technique used here is described in "Exploiting Pointer and Location Equivalence to Optimize Pointer Analysis. In the 14th International Static Analysis Symposium (SAS), August 2007." It is known as the "HVN" algorithm, and is equivalent to value numbering the collapsed constraint graph without evaluating unions. This is used as a pre-pass to HU in order to resolve first order pointer dereferences and speed up/reduce memory usage of HU. Running both is equivalent to HRU without the iteration
void Andersen::hvn()
{
	// Build a predecessor graph.  This is like our constraint graph with the edges going in the opposite direction, and there are edges for all the constraints, instead of just copy constraints.  We also build implicit edges for constraints are implied but not explicit.  I.E for the constraint a = &b, we add implicit edges *a = b.  This helps us capture more cycles
	DenseMap<NodeIndex, SparseBitVector<>> predGraph;
	DenseSet<NodeIndex> indirectNodes;

	for (auto const& c: constraints)
	{
		switch (c.getType())
		{
			case AndersConstraint::ADDR_OF:
			{
				indirectNodes.insert(c.getSrc());
				// Dest = &src edge
				predGraph[c.getDest()].set(getAdrNodeIndex(c.getSrc()));
				// *Dest = src edge
				predGraph[getRefNodeIndex(c.getDest())].set(c.getSrc());
				break;
			}
			case AndersConstraint::LOAD:
			{
				// dest = *src edge
				if (c.getOffset() == 0)
					predGraph[c.getDest()].set(getRefNodeIndex(c.getSrc()));
				else
					indirectNodes.insert(c.getDest());
			}
			case AndersConstraint::STORE:
			{
				// *dest = src edge
				if (c.getOffset() == 0)
					predGraph[getRefNodeIndex(c.getDest())].set(c.getSrc());
			}
			case AndersConstraint::COPY:
			{
				if (c.getOffset() == 0)
				{
					// Dest = Src edge
					predGraph[c.getDest()].set(c.getSrc());
					// *Dest = *Src edge
					predGraph[getRefNodeIndex(c.getDest())].set(getRefNodeIndex(c.getSrc()));
				}
				else
					indirectNodes.insert(c.getDest());
			}
		}
	}

	// Now run Tarjan's SCC algorithm to find cycles, condense predGraph, and explore possible equivalance relations

	// The SCC stack
	std::stack<unsigned> sccStack;
	// Map from NodeIndex to DFS number, and negative DFS number means never visited
	std::vector<int> dfsNum(nodeFactory.getNumNodes(), -1);
	// Well, as the name suggests, this set contains all nodes to be deleted
	DenseSet<NodeIndex> nodeToDelete;
	// Store the "representative" (or "leader") when there is a merge
	std::vector<NodeIndex> mergeTarget(nodeFactory.getNumNodes());
	for (unsigned i = 0, e = mergeTarget.size(); i < e; ++i)
		mergeTarget[i] = i;

	for (unsigned i = 0, e = nodeFactory.getNumNodes(); i < e; ++i)
	{
		NodeIndex rep = mergeTarget[i];
		if (dfsNum[rep] < 0)
			hvnVisit(rep);
	}
}

void Andersen::hvnVisit(NodeIndex n)
{
	
}
