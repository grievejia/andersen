#include "Andersen.h"
#include "CycleDetector.h"
#include "SparseBitVectorGraph.h"

#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/DenseSet.h"
#include "llvm/ADT/SmallSet.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/ADT/iterator_range.h"

#include <queue>
#include <map>

using namespace llvm;

namespace {

// This class represent the constraint graph
class ConstraintGraphNode
{
private:
	NodeIndex idx;

	// We use set rather than SmallSet because we need the capability of iteration
	typedef std::set<NodeIndex> NodeSet;
	NodeSet copyEdges, loadEdges, storeEdges;

	bool insertCopyEdge(NodeIndex dst)
	{
		return copyEdges.insert(dst).second;
	}
	bool insertLoadEdge(NodeIndex dst)
	{
		return loadEdges.insert(dst).second;
	}
	bool insertStoreEdge(NodeIndex dst)
	{
		return storeEdges.insert(dst).second;
	}

	ConstraintGraphNode(NodeIndex i): idx(i) {}
public:
	typedef NodeSet::iterator iterator;
	typedef NodeSet::const_iterator const_iterator;

	NodeIndex getNodeIndex() const { return idx; }

	iterator begin() { return copyEdges.begin(); }
	iterator end() { return copyEdges.end(); }
	const_iterator begin() const { return copyEdges.begin(); }
	const_iterator end() const { return copyEdges.end(); }

	const_iterator load_begin() const { return loadEdges.begin(); }
	const_iterator load_end() const { return loadEdges.end(); }
	llvm::iterator_range<const_iterator> loads() const
	{
		return llvm::iterator_range<const_iterator>(load_begin(), load_end());
	}

	const_iterator store_begin() const { return storeEdges.begin(); }
	const_iterator store_end() const { return storeEdges.end(); }
	llvm::iterator_range<const_iterator> stores() const
	{
		return llvm::iterator_range<const_iterator>(store_begin(), store_end());
	}

	friend class ConstraintGraph;
};

class ConstraintGraph
{
private:
	typedef std::map<NodeIndex, ConstraintGraphNode> NodeMapTy;
	NodeMapTy graph;
public:
	typedef NodeMapTy::iterator iterator;
	typedef NodeMapTy::const_iterator const_iterator;

	ConstraintGraph() {}

	bool insertCopyEdge(NodeIndex src, NodeIndex dst)
	{
		auto itr = graph.find(src);
		if (itr == graph.end())
		{
			ConstraintGraphNode srcNode(src);
			srcNode.insertCopyEdge(dst);
			graph.insert(std::make_pair(src, std::move(srcNode)));
			return true;
		}
		else
			return (itr->second).insertCopyEdge(dst);
	}

	bool insertLoadEdge(NodeIndex src, NodeIndex dst)
	{
		auto itr = graph.find(src);
		if (itr == graph.end())
		{
			ConstraintGraphNode srcNode(src);
			srcNode.insertLoadEdge(dst);
			graph.insert(std::make_pair(src, std::move(srcNode)));
			return true;
		}
		else
			return (itr->second).insertLoadEdge(dst);
	}

	bool insertStoreEdge(NodeIndex src, NodeIndex dst)
	{
		auto itr = graph.find(src);
		if (itr == graph.end())
		{
			ConstraintGraphNode srcNode(src);
			srcNode.insertStoreEdge(dst);
			graph.insert(std::make_pair(src, std::move(srcNode)));
			return true;
		}
		else
			return (itr->second).insertStoreEdge(dst);
	}

	ConstraintGraphNode* getNodeWithIndex(NodeIndex idx)
	{
		auto itr = graph.find(idx);
		if (itr == graph.end())
			return nullptr;
		else
			return &(itr->second);
	}

	ConstraintGraphNode* getOrInsertNode(NodeIndex idx)
	{
		auto itr = graph.find(idx);
		if (itr == graph.end())
		{
			ConstraintGraphNode newNode(idx);
			itr = graph.insert(std::make_pair(idx, newNode)).first;
		}
		return &(itr->second);
	}

	iterator begin() { return graph.begin(); }
	iterator end() { return graph.end(); }
	const_iterator begin() const { return graph.begin(); }
	const_iterator end() const { return graph.end(); }
};

}	// Temporary end of anonymous namespace

// Specialize the AnderGraphTraits for ConstraintGraph
template <> class AndersGraphTraits<ConstraintGraph>
{
public:
	typedef ConstraintGraphNode NodeType;
	typedef MapValueIterator<ConstraintGraph::const_iterator> NodeIterator;
	typedef ConstraintGraphNode::iterator ChildIterator;

	static inline ChildIterator child_begin(const NodeType* n)
	{
		return n->begin();
	}
	static inline ChildIterator child_end(const NodeType* n)
	{
		return n->end();
	}

	static inline NodeIterator node_begin(const ConstraintGraph* g)
	{
		return NodeIterator(g->begin());
	}
	static inline NodeIterator node_end(const ConstraintGraph* g)
	{
		return NodeIterator(g->end());
	}
};

namespace {

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

// The technique used here is described in "The Ant and the Grasshopper: Fast and Accurate Pointer Analysis for Millions of Lines of Code. In Programming Language Design and Implementation (PLDI), June 2007." It is known as the "HCD" (Hybrid Cycle Detection) algorithm. It is called a hybrid because it performs an offline analysis and uses its results during the solving (online) phase. This is just the offline portion
class OfflineCycleDetector: public CycleDetector<SparseBitVectorGraph>
{
private:
	// The node factory
	AndersNodeFactory& nodeFactory;

	// The offline constraint graph
	SparseBitVectorGraph offlineGraph;
	// If a mapping <p, q> is in this map, it means that *p and q are in the same cycle in the offline constraint graph, and anything that p points to during the online constraint solving phase can be immediately collapse with q
	DenseMap<NodeIndex, NodeIndex> collapseMap;
	// Holds the pairs of VAR nodes that we are going to merge together
	DenseMap<NodeIndex, NodeIndex> mergeMap;
	// Used to collect the scc nodes on a cycle
	SparseBitVector<> scc;

	// Return the node index of the "ref node" (used to represent *n) of n. 
	// We won't actually create that ref node. We cannot use the NodeIndex of that refNode to index into nodeFactory
	NodeIndex getRefNodeIndex(NodeIndex n) const
	{
		return n + nodeFactory.getNumNodes();
	}

	void buildOfflineConstraintGraph(const std::vector<AndersConstraint>& constraints)
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
					offlineGraph.insertEdge(getRefNodeIndex(srcTgt), dstTgt);
					break;
				}
				case AndersConstraint::STORE:
				{
					offlineGraph.insertEdge(srcTgt, getRefNodeIndex(dstTgt));
					break;
				}
				case AndersConstraint::COPY:
				{
					offlineGraph.insertEdge(srcTgt, dstTgt);
					break;
				}
			}
		}
	}
	
	NodeType* getRep(NodeIndex idx) override
	{
		return offlineGraph.getOrInsertNode(idx);
	}

	// Specify how to process the non-rep nodes if a cycle is found
	void processNodeOnCycle(const NodeType* node, const NodeType* repNode) override
	{
		scc.set(node->getNodeIndex());
	}

	// Specify how to process the rep nodes if a cycle is found
	void processCycleRepNode(const NodeType* node) override
	{
		// A trivial cycle is not interesting
		if (scc.count() == 0)
			return;

		scc.set(node->getNodeIndex());

		// The representative is the first non-ref node
		NodeIndex repNode = scc.find_first();
		assert(repNode < nodeFactory.getNumNodes() && "The SCC didn't have a non-Ref node!");
		for (auto itr = ++scc.begin(), ite = scc.end(); itr != ite; ++itr)
		{
			NodeIndex cycleNode = *itr;
			if (cycleNode > nodeFactory.getNumNodes())
				// For REF nodes, insert it to the collapse map
				collapseMap[cycleNode - nodeFactory.getNumNodes()] = repNode;
			else
				// For VAR nodes, insert it to the merge map
				// We don't merge the nodes immediately to avoid affecting the DFS
				mergeMap[cycleNode] = repNode;
		}

		scc.clear();
	}

public:
	OfflineCycleDetector(const std::vector<AndersConstraint>& cs, AndersNodeFactory& n): nodeFactory(n)
	{
		// Build the offline constraint graph first before we move on
		buildOfflineConstraintGraph(cs);
	}

	void run() override
	{
		runOnGraph(&offlineGraph);

		// Merge the nodes in mergeMap
		for (auto const& mapping: mergeMap)
			nodeFactory.mergeNode(mapping.second, mapping.first);

		// We don't need these structures any more. The only thing we keep should be those info that are necessary to answer collapsing target queries
		mergeMap.clear();
		offlineGraph.releaseMemory();
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
				cGraph.insertLoadEdge(srcTgt, dstTgt);
				break;
			}
			case AndersConstraint::STORE:
			{
				cGraph.insertStoreEdge(dstTgt, srcTgt);
				break;
			}
			case AndersConstraint::COPY:
			{
				cGraph.insertCopyEdge(srcTgt, dstTgt);
				break;
			}
		}
	}
}

class OnlineCycleDetector: public CycleDetector<ConstraintGraph>
{
private:
	AndersNodeFactory& nodeFactory;
	ConstraintGraph& constraintGraph;
	DenseMap<NodeIndex, AndersPtsSet>& ptsGraph;
	const DenseSet<NodeIndex>& candidates;

	NodeType* getRep(NodeIndex idx) override
	{
		return constraintGraph.getOrInsertNode(nodeFactory.getMergeTarget(idx));
	}
	// Specify how to process the non-rep nodes if a cycle is found
	void processNodeOnCycle(const NodeType* node, const NodeType* repNode) override
	{
		NodeIndex repIdx = repNode->getNodeIndex();
		NodeIndex cycleIdx = node->getNodeIndex();
		nodeFactory.mergeNode(repIdx, cycleIdx);
		ptsGraph[repIdx].unionWith(ptsGraph[cycleIdx]);
	}
	// Specify how to process the rep nodes if a cycle is found
	void processCycleRepNode(const NodeType* node) override
	{
		// Do nothing, I guess?
	}

public:
	OnlineCycleDetector(AndersNodeFactory& n, ConstraintGraph& co, DenseMap<NodeIndex, AndersPtsSet>& p, const DenseSet<NodeIndex>& ca): nodeFactory(n), constraintGraph(co), ptsGraph(p), candidates(ca) {}

	void run() override
	{
		// Perform cycle detection on for nodes on the candidate list
		for (auto node: candidates)
			runOnNode(node);
	}
};

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
		if (nodeFactory.getMergeTarget(i) == i && ptsGraph.count(i) && constraintGraph.getNodeWithIndex(i) != nullptr)
			currWorkList->enqueue(i);
	}

	while (!currWorkList->isEmpty())
	{
		// Iteration begins

		// First we've got to check if there is any cycle candidates in the last iteration. If there is, detect and collapse cycle
		if (!cycleCandidates.empty())
		{
			// Detect and collapse cycles online
			OnlineCycleDetector cycleDetector(nodeFactory, constraintGraph, ptsGraph, cycleCandidates);
			cycleDetector.run();
			cycleCandidates.clear();
		}

		while (!currWorkList->isEmpty())
		{
			NodeIndex node = currWorkList->dequeue();
			node = nodeFactory.getMergeTarget(node);
			//errs() << "Examining node " << node << "\n";

			ConstraintGraphNode* cNode = constraintGraph.getNodeWithIndex(node);
			if (cNode == nullptr)
				continue;

			auto ptsItr = ptsGraph.find(node);
			if (ptsItr != ptsGraph.end())
			{
				// Check indirect constraints and add copy edge to the constraint graph if necessary
				const AndersPtsSet& ptsSet = ptsItr->second;
				for (auto v: ptsSet)
				{
					for (auto const& dst: cNode->loads())
					{
						NodeIndex tgtNode = nodeFactory.getMergeTarget(dst);
						//errs() << "Examining load edge " << node << " -> " << tgtNode << ", offset = " << offset << "\n";
						if (constraintGraph.insertCopyEdge(v, tgtNode))
						{
							//errs() << "\tInsert copy edge " << v << " -> " << tgtNode << ", offset = " << offset << "\n";
							nextWorkList->enqueue(v);
						}
					}

					for (auto const& dst: cNode->stores())
					{
						NodeIndex tgtNode = nodeFactory.getMergeTarget(dst);
						if (constraintGraph.insertCopyEdge(tgtNode, v))
						{
							//errs() << "\tInsert copy edge " << tgtNode << " -> " << v << ", offset = " << offset << "\n";
							nextWorkList->enqueue(tgtNode);
						}
					}
				}
				
				// Finally, it's time to propagate pts-to info along the copy edges
				for (auto const& dst: *cNode)
				{
					NodeIndex tgtNode = nodeFactory.getMergeTarget(dst);
					AndersPtsSet& tgtPtsSet = ptsGraph[tgtNode];
					// Here we need to re-compute ptsSet because the previous line may cause an map insertion, which will invalidate any existing map iterators
					const AndersPtsSet& ptsSet = ptsGraph[node];
					bool isChanged =  tgtPtsSet.unionWith(ptsSet);

					if (isChanged)
					{
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
