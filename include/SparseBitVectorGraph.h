#ifndef ANDERSEN_SPARSEBITVECTOR_GRAPH_H
#define ANDERSEN_SPARSEBITVECTOR_GRAPH_H

#include "GraphTraits.h"
#include "NodeFactory.h"

#include "llvm/ADT/SparseBitVector.h"
#include "llvm/ADT/DenseMap.h"

#include <algorithm>
#include <unordered_map>

// The node of a graph class where successor edges are represented by sparse bit vectors
class SparseBitVectorGraphNode
{
private:
	NodeIndex idx;
	llvm::SparseBitVector<> succs;

	void insertEdge(NodeIndex n) { return succs.set(n); }

	SparseBitVectorGraphNode(NodeIndex i): idx(i) {}
public:
	using iterator = llvm::SparseBitVector<>::iterator;

	NodeIndex getNodeIndex() const { return idx; }

	iterator begin() const { return succs.begin(); }
	iterator end() const { return succs.end(); }

	unsigned succ_getSize() const { return succs.count(); }

	friend class SparseBitVectorGraph;
};

// A graph class where successor edges are represented by sparse bit vectors
class SparseBitVectorGraph
{
private:
	// Here we cannot use DenseMap because we need iterator stability: we might want to call getOrInsertNode() when another node is being iterated
	using NodeMapTy = std::unordered_map<NodeIndex, SparseBitVectorGraphNode>;
	NodeMapTy graph;
public:
	using iterator = NodeMapTy::iterator;
	using const_iterator = NodeMapTy::const_iterator;
private:
	iterator getOrInsertNodeMap(NodeIndex idx)
	{
		auto itr = graph.find(idx);
		if (itr == graph.end())
			itr = graph.insert(std::make_pair(idx, SparseBitVectorGraphNode(idx))).first;
		return itr;
	}
public:
	SparseBitVectorGraph() {}

	SparseBitVectorGraphNode* getOrInsertNode(NodeIndex idx)
	{
		auto itr = getOrInsertNodeMap(idx);
		return &(itr->second);
	}

	void insertEdge(NodeIndex src, NodeIndex dst)
	{
		auto itr = getOrInsertNodeMap(src);
		(itr->second).insertEdge(dst);
	}

	// src's successors += dst's successors
	void mergeEdge(NodeIndex src, NodeIndex dst)
	{
		auto dstItr = graph.find(dst);
		if (dstItr == graph.end())
			return;

		auto srcItr = getOrInsertNodeMap(src);
		(srcItr->second).succs |= (dstItr->second).succs;
	}

	SparseBitVectorGraphNode* getNodeWithIndex(NodeIndex idx)
	{
		auto itr = graph.find(idx);
		if (itr == graph.end())
			return nullptr;
		else
			return &(itr->second);
	}

	unsigned getSize() const { return graph.size(); }

	void releaseMemory()
	{
		graph.clear();
	}

	iterator begin() { return graph.begin(); }
	iterator end() { return graph.end(); }
	const_iterator begin() const { return graph.begin(); }
	const_iterator end() const { return graph.end(); }
};

// Specialize the AnderGraphTraits for SparseBitVectorGraph
template <> class AndersGraphTraits<SparseBitVectorGraph>
{
public:
	typedef SparseBitVectorGraphNode NodeType;
	typedef MapValueIterator<SparseBitVectorGraph::iterator> NodeIterator;
	typedef SparseBitVectorGraphNode::iterator ChildIterator;

	static inline ChildIterator child_begin(NodeType* n)
	{
		return n->begin();
	}
	static inline ChildIterator child_end(NodeType* n)
	{
		return n->end();
	}

	static inline NodeIterator node_begin(SparseBitVectorGraph* g)
	{
		return NodeIterator(g->begin());
	}
	static inline NodeIterator node_end(SparseBitVectorGraph* g)
	{
		return NodeIterator(g->end());
	}
};

#endif
