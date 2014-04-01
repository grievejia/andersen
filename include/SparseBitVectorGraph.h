#ifndef ANDERSEN_SPARSEBITVECTOR_GRAPH_H
#define ANDERSEN_SPARSEBITVECTOR_GRAPH_H

#include "GraphTraits.h"
#include "NodeFactory.h"

#include "llvm/ADT/SparseBitVector.h"
#include "llvm/ADT/DenseMap.h"

#include <algorithm>

// The node of a graph class where successor edges are represented by sparse bit vectors
class SparseBitVectorGraphNode
{
private:
	NodeIndex idx;
	llvm::SparseBitVector<> succs;

	void insertEdge(NodeIndex n) { return succs.set(n); }

	SparseBitVectorGraphNode(NodeIndex i): idx(i) {}
public:
	typedef llvm::SparseBitVector<>::iterator iterator;

	NodeIndex getNodeIndex() const { return idx; }

	iterator begin() const { return succs.begin(); }
	iterator end() const { return succs.end(); }

	friend class SparseBitVectorGraph;
};

// A graph class where successor edges are represented by sparse bit vectors
class SparseBitVectorGraph
{
private:
	// Here we cannot use DenseMap because we need iterator stability: we might want to call getOrInsertNode() when another node is being iterated
	typedef std::map<NodeIndex, SparseBitVectorGraphNode> NodeMapTy;
	NodeMapTy graph;
public:
	typedef NodeMapTy::iterator iterator;
	typedef NodeMapTy::const_iterator const_iterator;

	SparseBitVectorGraph() {}

	void insertEdge(NodeIndex src, NodeIndex dst)
	{
		auto itr = graph.find(src);
		if (itr == graph.end())
		{
			SparseBitVectorGraphNode srcNode(src);
			itr = graph.insert(std::make_pair(src, std::move(srcNode))).first;
		}
		(itr->second).insertEdge(dst);
	}

	// src's successors += dst's successors
	void mergeEdge(NodeIndex src, NodeIndex dst)
	{
		auto dstItr = graph.find(dst);
		if (dstItr == graph.end())
			return;

		auto srcItr = graph.find(src);
		if (srcItr == graph.end())
		{
			SparseBitVectorGraphNode srcNode(src);
			srcItr = graph.insert(std::make_pair(src, std::move(srcNode))).first;
		}
		(srcItr->second).succs |= (dstItr->second).succs;
	}

	SparseBitVectorGraphNode* getOrInsertNode(NodeIndex idx)
	{
		auto itr = graph.find(idx);
		if (itr == graph.end())
		{
			SparseBitVectorGraphNode newNode(idx);
			itr = graph.insert(std::make_pair(idx, newNode)).first;
		}
		return &(itr->second);
	}

	SparseBitVectorGraphNode* getNodeWithIndex(NodeIndex idx)
	{
		auto itr = graph.find(idx);
		if (itr == graph.end())
			return nullptr;
		else
			return &(itr->second);
	}

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
