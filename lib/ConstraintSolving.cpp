#include "Andersen.h"

#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/DenseSet.h"
#include "llvm/ADT/SmallSet.h"
#include "llvm/ADT/SparseBitVector.h"
#include "llvm/Support/raw_ostream.h"

#include <queue>
#include <map>

using namespace llvm;

namespace {

// ConstraintEdge = Node + Offset
typedef std::pair<NodeIndex, unsigned> ConstraintEdge;

// This class represent the constraint graph
// It only support edge insertion and edge query, which is enough for our purpose
class ConstraintGraph
{
public:
	struct EdgeSets
	{
		std::set<ConstraintEdge> copyEdges, loadEdges, storeEdges;
	};
private:
	std::map<NodeIndex, EdgeSets> graph;
public:
	ConstraintGraph() {}

	bool insertCopyEdge(NodeIndex src, NodeIndex dst, unsigned offset)
	{
		return graph[src].copyEdges.insert(std::make_pair(dst, offset)).second;
	}
	bool insertLoadEdge(NodeIndex src, NodeIndex dst, unsigned offset)
	{
		return graph[src].loadEdges.insert(std::make_pair(dst, offset)).second;
	}
	bool insertStoreEdge(NodeIndex src, NodeIndex dst, unsigned offset)
	{
		return graph[src].storeEdges.insert(std::make_pair(dst, offset)).second;
	}

	const EdgeSets* getSuccessors(NodeIndex n)
	{
		auto itr = graph.find(n);
		if (itr != graph.end())
			return &(itr->second);
		else
			return nullptr;
	}
};

// The worklist for our analysis
class AndersWorkList
{
private:
	// The FIFO queue
	std::queue<NodeIndex> list;
	// Avoid duplicate entries in FIFO queue
	llvm::SmallSet<NodeIndex, 16> set;
public:
	AndersWorkList() {}
	void enqueue(NodeIndex elem)
	{
		if (!set.count(elem))
		{
			list.push(elem);
			set.insert(elem);
		}
	}
	NodeIndex dequeue()
	{
		assert(!list.empty() && "Trying to dequeue an empty queue!");
		NodeIndex ret = list.front();
		list.pop();
		set.erase(ret);
		return ret;
	}
	bool isEmpty() const { return list.empty(); }
};

// A base class that offers the functionality of detecting SCC in a graph
class CycleDetector
{
protected:
	AndersNodeFactory& nodeFactory;
	// Number of nodes
	unsigned numNodes;

	// The SCC stack
	std::stack<unsigned> sccStack;
	// Map from NodeIndex to DFS number, and negative DFS number means never visited
	std::vector<int> dfsNum;
	// The "inComponent" array in Nutilla's improved SCC algorithm
	std::deque<bool> inComponent;
	// DFS timestamp
	int timestamp;

	// Return true if node has successors in the graph
	virtual bool hasSuccessor(NodeIndex node) = 0;
	// Return the successors of node
	virtual const SparseBitVector<>& getSuccessors(NodeIndex node) = 0;
	// Sometimes we need to adjust the node index before we moving on traversing that node
	virtual NodeIndex getRep(NodeIndex node) = 0;
	// The function specifying how to process a cycle
	virtual void processCycle(NodeIndex node, int myTimeStamp) = 0;

	// visiting each node and perform some task
	void visit(NodeIndex node)
	{
		assert(node < numNodes && "Visiting a non-existent node!");
		int myTimeStamp = timestamp++;
		dfsNum[node] = myTimeStamp;

		// Traverse succecessor edges
		if (hasSuccessor(node))
		{
			auto edges = getSuccessors(node);
			for (auto const& succ: edges)
			{
				NodeIndex succRep = getRep(succ);
				if (dfsNum[succRep] < 0)
					visit(succRep);

				if (!inComponent[succRep] && dfsNum[node] > dfsNum[succRep])
					dfsNum[node] = dfsNum[succRep];
			}
		}

		// See if we have any cycle detected
		if (myTimeStamp != dfsNum[node])
		{
			// If not, push the sccStack and go on
			sccStack.push(node);
			return;
		}

		// Cycle detected
		inComponent[node] = true;
		processCycle(node, myTimeStamp);
	}

	void releaseSCCMemory()
	{
		dfsNum.clear();
		inComponent.clear();
	}
public:
	CycleDetector(AndersNodeFactory& n, unsigned nn): nodeFactory(n), numNodes(nn), dfsNum(numNodes), inComponent(numNodes), timestamp(0) {}

	// The public interface of running the detector
	virtual void run() = 0;
};

// The technique used here is described in "The Ant and the Grasshopper: Fast and Accurate Pointer Analysis for Millions of Lines of Code. In Programming Language Design and Implementation (PLDI), June 2007." It is known as the "HCD" (Hybrid Cycle Detection) algorithm. It is called a hybrid because it performs an offline analysis and uses its results during the solving (online) phase. This is just the offline portion
class OfflineCycleDetector: public CycleDetector
{
private:
	std::vector<AndersConstraint>& constraints;

	// The offline constraint graph
	DenseMap<NodeIndex, SparseBitVector<>> offlineGraph;
	// If a mapping <p, q> is in this map, it means that *p and q are in the same cycle in the offline constraint graph, and anything that p points to during the online constraint solving phase can be immediately collapse with q
	DenseMap<NodeIndex, NodeIndex> collapseMap;
	// Holds the pairs of VAR nodes that we are going to merge together
	DenseMap<NodeIndex, NodeIndex> mergeMap;

	// Return the node index of the "ref node" (used to represent *n) of n. 
	// We won't actually create that ref node. We cannot use the NodeIndex of that refNode to index into nodeFactory
	NodeIndex getRefNodeIndex(NodeIndex n) const
	{
		return n + nodeFactory.getNumNodes();
	}

	void buildOfflineConstraintGraph()
	{
		for (auto const& c: constraints)
		{
			NodeIndex srcTgt = nodeFactory.getMergeTarget(c.getSrc());
			NodeIndex dstTgt = nodeFactory.getMergeTarget(c.getDest());
			switch (c.getType())
			{
				case AndersConstraint::ADDR_OF:
					break;
				case AndersConstraint::LOAD:
				{
					if (c.getOffset() == 0)
						offlineGraph[dstTgt].set(getRefNodeIndex(srcTgt));
					break;
				}
				case AndersConstraint::STORE:
				{
					if (c.getOffset() == 0)
						offlineGraph[getRefNodeIndex(dstTgt)].set(srcTgt);
					break;
				}
				case AndersConstraint::COPY:
				{
					if (c.getOffset() == 0)
						offlineGraph[dstTgt].set(srcTgt);
					break;
				}
			}
		}
	}

	bool hasSuccessor(NodeIndex node) override
	{
		return offlineGraph.count(node);
	}
	
	const SparseBitVector<>& getSuccessors(NodeIndex node) override
	{
		return offlineGraph[node];
	}
	
	NodeIndex getRep(NodeIndex succ) override
	{
		if (succ > nodeFactory.getNumNodes())
			return getRefNodeIndex(succ % nodeFactory.getNumNodes());
		else
			return nodeFactory.getMergeTarget(succ);
	}

	void processCycle(NodeIndex node, int myTimeStamp) override
	{
		SparseBitVector<> scc;
		while (!sccStack.empty())
		{
			NodeIndex cycleNode = sccStack.top();
			if (dfsNum[cycleNode] < myTimeStamp)
				break;

			scc.set(cycleNode);
			sccStack.pop();
		}

		// A trivial cycle is not interesting
		if (scc.count() == 1)
			return;

		// The representative is the first non-ref node
		NodeIndex repNode = scc.find_first();
		assert(repNode < nodeFactory.getNumNodes() && "The SCC didn't have a non-Ref node!");
		for (auto itr = ++scc.begin(), ite = scc.end(); itr != ite; ++itr)
		{
			NodeIndex cycleNode = *itr;
			if (cycleNode > nodeFactory.getNumNodes())
				// For REF nodes, insert it to the collapse map
				collapseMap[cycleNode - nodeFactory.getNumNodes()] = node;
			else
				// For VAR nodes, insert it to the merge map
				// We don't merge the nodes immediately to avoid affecting the DFS
				mergeMap[cycleNode] = node;
		}
	}

public:
	OfflineCycleDetector(std::vector<AndersConstraint>& c, AndersNodeFactory& n): CycleDetector(n, n.getNumNodes() * 2), constraints(c) {}

	void run() override
	{
		// Build the offline constraint graph first
		buildOfflineConstraintGraph();

		// Now run Tarjan's SCC algorithm to find cycles and mark potential collapsing target
		for (unsigned i = 0, e = nodeFactory.getNumNodes(); i < e; ++i)
		{
			NodeIndex rep = nodeFactory.getMergeTarget(i);
			if (dfsNum[rep] < 0)
				visit(rep);
			if (dfsNum[getRefNodeIndex(rep)] < 0)
				visit(getRefNodeIndex(rep));
		}

		// Merge the nodes in mergeMap
		for (auto const& mapping: mergeMap)
			nodeFactory.mergeNode(mapping.second, mapping.first);

		// We don't need these structures any more. The only thing we keep should be those info that are necessary to answer collapsing target queries
		offlineGraph.clear();
		mergeMap.clear();
		releaseSCCMemory();
	}

	// Return InvalidIndex if no collapse target found
	NodeIndex getCollapseTarget(NodeIndex n)
	{
		auto itr = collapseMap.find(n);
		if (itr == collapseMap.end())
			return AndersNodeFactory::InvalidIndex;
		else
			return itr->second;
	}
};

void buildConstraintGraph(ConstraintGraph& cGraph, const std::vector<AndersConstraint>& constraints, AndersNodeFactory& nodeFactory, DenseMap<NodeIndex, AndersPtsSet>& ptsGraph)
{
	for (auto const& c: constraints)
	{
		NodeIndex srcTgt = nodeFactory.getMergeTarget(c.getSrc());
		NodeIndex dstTgt = nodeFactory.getMergeTarget(c.getDest());
		switch (c.getType())
		{
			case AndersConstraint::ADDR_OF:
			{
				// We don't want to replace src with srcTgt because, after all, the address of a variable is NOT the same as the address of another variable
				ptsGraph[dstTgt].insert(c.getSrc());
				break;
			}
			case AndersConstraint::LOAD:
			{
				cGraph.insertLoadEdge(srcTgt, dstTgt, c.getOffset());
				break;
			}
			case AndersConstraint::STORE:
			{
				cGraph.insertStoreEdge(dstTgt, srcTgt, c.getOffset());
				break;
			}
			case AndersConstraint::COPY:
			{
				cGraph.insertCopyEdge(srcTgt, dstTgt, c.getOffset());
				break;
			}
		}
	}
}


}	// end of anonymous namespace

/// solveConstraints - This stage iteratively processes the constraints list
/// propagating constraints (adding edges to the Nodes in the points-to graph)
/// until a fixed point is reached.
///
/// We use a variant of the technique called "Lazy Cycle Detection", which is
/// described in "The Ant and the Grasshopper: Fast and Accurate Pointer
/// Analysis for Millions of Lines of Code. In Programming Language Design and
/// Implementation (PLDI), June 2007."
/// The paper describes performing cycle detection one node at a time, which can
/// be expensive if there are no cycles, but there are long chains of nodes that
/// it heuristically believes are cycles (because it will DFS from each node
/// without state from previous nodes).
/// Instead, we use the heuristic to build a worklist of nodes to check, then
/// cycle detect them all at the same time to do this more cheaply.  This
/// catches cycles slightly later than the original technique did, but does it
/// make significantly cheaper.
void Andersen::solveConstraints()
{
	// We'll do offline HCD first
	OfflineCycleDetector offlineInfo(constraints, nodeFactory);
	offlineInfo.run();

	// Now build the constraint graph
	ConstraintGraph constraintGraph;
	buildConstraintGraph(constraintGraph, constraints, nodeFactory, ptsGraph);
	// The constraint vector is useless now
	constraints.clear();

	// We switch between two work lists instead of relying on only one work list
	AndersWorkList workList1, workList2;
	// The "current" and the "next" work list
	AndersWorkList *currWorkList = &workList1, *nextWorkList = &workList2;
	// The set of nodes that LCD believes might be on a cycle
	DenseSet<NodeIndex> cycleCandidates;
	// The set of edges that LCD believes not on a cycle
	DenseSet<std::pair<NodeIndex, NodeIndex>> checkedEdges;

	// Scan the node list, add it to work list if the node a representative and can contribute to the calculation right now.
	for (unsigned i = 0, e = nodeFactory.getNumNodes(); i < e; ++i)
	{
		if (nodeFactory.getMergeTarget(i) == i && ptsGraph.count(i) && constraintGraph.getSuccessors(i) != nullptr)
			currWorkList->enqueue(i);
	}

	while (!currWorkList->isEmpty())
	{
		// Iteration begins

		// First we've got to check if there is any cycle candidates in the last iteration. If there is, detect and collapse cycle
		if (!cycleCandidates.empty())
		{
			// Once again, since there are too many states to keep track of when detecting cycles, we use another class to do all the work
			//OnlineCycleDetector cycleDetector(nodeFactory, constraintGraph, cycleCandidates);
			//cycleDetector.run();
			//cycleCandidates.clear();
		}

		while (!currWorkList->isEmpty())
		{
			NodeIndex node = currWorkList->dequeue();
			node = nodeFactory.getMergeTarget(node);
			//errs() << "Examining node " << node << "\n";

			auto graphSucc = constraintGraph.getSuccessors(node);

			auto ptsItr = ptsGraph.find(node);
			if (ptsItr != ptsGraph.end() && graphSucc != nullptr)
			{
				// Check indirect constraints and add copy edge to the constraint graph if necessary
				const AndersPtsSet& ptsSet = ptsItr->second;
				for (auto v: ptsSet)
				{
					for (auto const& edge: graphSucc->loadEdges)
					{
						NodeIndex tgtNode = edge.first;
						tgtNode = nodeFactory.getMergeTarget(tgtNode);
						unsigned offset = edge.second;
						//errs() << "Examining load edge " << node << " -> " << tgtNode << ", offset = " << offset << "\n";
						if (constraintGraph.insertCopyEdge(v, tgtNode, offset))
						{
							//errs() << "\tInsert copy edge " << v << " -> " << tgtNode << ", offset = " << offset << "\n";
							nextWorkList->enqueue(v);
						}
					}

					for (auto const& edge: graphSucc->storeEdges)
					{
						NodeIndex tgtNode = edge.first;
						tgtNode = nodeFactory.getMergeTarget(tgtNode);
						unsigned offset = edge.second;
						if (constraintGraph.insertCopyEdge(tgtNode, v, offset))
						{
							//errs() << "\tInsert copy edge " << tgtNode << " -> " << v << ", offset = " << offset << "\n";
							nextWorkList->enqueue(tgtNode);
						}
					}
				}
				
				// Finally, it's time to propagate pts-to info along the copy edges
				for (auto const& edge: graphSucc->copyEdges)
				{
					NodeIndex tgtNode = edge.first;
					tgtNode = nodeFactory.getMergeTarget(tgtNode);
					unsigned offset = edge.second;
					AndersPtsSet& tgtPtsSet = ptsGraph[tgtNode];
					// Here we need to re-compute ptsSet because the previous line may cause an map insertion, which will invalidate any existing map iterators
					const AndersPtsSet& ptsSet = ptsGraph[node];
					bool isChanged = false;
					if (offset == 0)
						isChanged = tgtPtsSet.unionWith(ptsSet);
					else
					{
						for (auto v: ptsSet)
						{
							if (v == nodeFactory.getUniversalObjNode())
							{
								isChanged |= tgtPtsSet.insert(v);
								break;
							}
							//assert(v + offset < nodeFactory.getNumNodes() && "Node index out of range!");
							isChanged |= tgtPtsSet.insert(v + offset);
						}
					}

					if (isChanged)
					{
						// If tgtPtsSet gets the universal object, just keep that object and discard everything else
						if (tgtPtsSet.has(nodeFactory.getUniversalObjNode()))
						{
							tgtPtsSet.clear();
							tgtPtsSet.insert(nodeFactory.getUniversalObjNode());
						}
						nextWorkList->enqueue(tgtNode);
					}
					else
					{
						// This is where we do lazy cycle detection.
						// If this is a cycle candidate (equal points-to sets and this particular edge has not been cycle-checked previously), add to the list to check for cycles on the next iteration
						auto edgePair = std::make_pair(node, tgtNode);
						if (!checkedEdges.count(edgePair) && ptsSet == tgtPtsSet)
						{
							checkedEdges.insert(edgePair);
							cycleCandidates.insert(tgtNode);
						}
					}
				}
			}
		}
		// Swap the current and the next worklist
		std::swap(currWorkList, nextWorkList);
	}
}
