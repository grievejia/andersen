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

void Andersen::solveConstraints()
{
	// Now build the constraint graph
	ConstraintGraph constraintGraph;
	buildConstraintGraph(constraintGraph, constraints, nodeFactory, ptsGraph);
	// The constraint vector is useless now
	constraints.clear();

	AndersWorkList workList;
	// Scan the node list, add it to work list if the node a representative and can contribute to the calculation right now.
	for (unsigned i = 0, e = nodeFactory.getNumNodes(); i < e; ++i)
	{
		if (nodeFactory.getMergeTarget(i) == i && ptsGraph.count(i) && constraintGraph.getSuccessors(i) != nullptr)
			workList.enqueue(i);
	}

	while (!workList.isEmpty())
	{
		NodeIndex node = workList.dequeue();
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
					unsigned offset = edge.second;
					//errs() << "Examining load edge " << node << " -> " << tgtNode << ", offset = " << offset << "\n";
					if (constraintGraph.insertCopyEdge(v, tgtNode, offset))
					{
						//errs() << "\tInsert copy edge " << v << " -> " << tgtNode << ", offset = " << offset << "\n";
						workList.enqueue(v);
					}
				}

				for (auto const& edge: graphSucc->storeEdges)
				{
					NodeIndex tgtNode = edge.first;
					unsigned offset = edge.second;
					if (constraintGraph.insertCopyEdge(tgtNode, v, offset))
					{
						//errs() << "\tInsert copy edge " << tgtNode << " -> " << v << ", offset = " << offset << "\n";
						workList.enqueue(tgtNode);
					}
				}
			}
			
			// Finally, it's time to propagate pts-to info along the copy edges
			for (auto const& edge: graphSucc->copyEdges)
			{
				NodeIndex tgtNode = edge.first;
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
					workList.enqueue(tgtNode);
				}
			}
		}
	}
}
