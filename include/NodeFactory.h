#ifndef ANDERSEN_NODE_FACTORY_H
#define ANDERSEN_NODE_FACTORY_H

#include "StructAnalyzer.h"

#include "llvm/IR/Value.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/DataLayout.h"
#include "llvm/IR/Constants.h"
#include "llvm/ADT/DenseMap.h"

#include <vector>
#include <limits>

// Node class - This class is used to represent a node in the constraint graph.  Due to various optimizations, it is not always the case that there is always a mapping from a Node to a Value. (In particular, we add artificial Node's that represent the set of pointed-to variables shared for each location equivalent Node.
// Ordinary clients are not allowed to create AndersNode objects. To guarantee index consistency, AndersNodes (and its subclasses) instances should only be created through AndersNodeFactory.
class AndersNode
{
public:
	enum AndersNodeType
	{
		VALUE_NODE,
		OBJ_NODE
	};
private:
	AndersNodeType type;
	unsigned idx;
	const llvm::Value* value;
	AndersNode(AndersNodeType t, unsigned i, const llvm::Value* v = NULL): type(t), idx(i), value(v) {}
public:
	unsigned getIndex() const { return idx; }
	const llvm::Value* getValue() const { return value; }

	friend class AndersNodeFactory;
};

typedef unsigned NodeIndex;

// This is the factory class of AndersNode
// It use a vectors to hold all Nodes in the program
// Since vectors may invalidate all element pointers/iterators when resizing, it is impossible to return AndersNode* in public interfaces (they may get invalidated). Therefore, we use plain integers to represent nodes for public functions like createXXX and getXXX. This is ugly, but it is efficient.
class AndersNodeFactory
{
public:
	// The largest unsigned int is reserved for invalid index
	static const unsigned InvalidIndex = std::numeric_limits<unsigned int>::max();
private:
	// The datalayout info
	const llvm::DataLayout* dataLayout;
	// The struct info
	const StructAnalyzer* structAnalyzer;

	// The set of nodes 
	std::vector<AndersNode> nodes;

	// Some special indices
	static const NodeIndex UniversalPtrIndex = 0;
	static const NodeIndex UniversalObjIndex = 1;
	static const NodeIndex NullPtrIndex = 2;
	static const NodeIndex NullObjectIndex = 3;

	// valueNodeMap - This map indicates the AndersNode* that a particular Value* corresponds to
	llvm::DenseMap<const llvm::Value*, NodeIndex> valueNodeMap;
	
	/// ObjectNodes - This map contains entries for each memory object in the program: globals, alloca's and mallocs.
	// We are able to represent them as llvm::Value* because we're modeling the heap with the simplest allocation-site approach
	llvm::DenseMap<const llvm::Value*, NodeIndex> objNodeMap;

	/// returnMap - This map contains an entry for each function in the
	/// program that returns a ptr.
	llvm::DenseMap<const llvm::Function*, NodeIndex> returnMap;

	/// varargMap - This map contains the entry used to represent all pointers
	/// passed through the varargs portion of a function call for a particular
	/// function.  An entry is not present in this map for functions that do not
	/// take variable arguments.
	llvm::DenseMap<const llvm::Function*, NodeIndex> varargMap;

	// gepMap - This map maintains the gep-relations across value nodes. The mappings are of the form <base-ptr, offset> -> gep-ptr, where base-ptr is the ValueNodeIndex for nodes that created out of llvm SSA variables while gep-ptr is the ValueNodeIndex for nodes that are created out of GEP instructions
	llvm::DenseMap<std::pair<NodeIndex, unsigned>, NodeIndex> gepMap;

	// Helper functions to do GEP translation
	unsigned offsetToFieldNum(const llvm::Value* ptr, unsigned offset) const;
	unsigned constGEPtoFieldNum(const llvm::ConstantExpr* expr) const;
public:
	AndersNodeFactory();

	void setDataLayout(const llvm::DataLayout* d) { dataLayout = d; }
	void setStructAnalyzer(const StructAnalyzer* s) { structAnalyzer = s; }

	// Factory methods
	NodeIndex createValueNode(const llvm::Value* val = NULL);
	NodeIndex createObjectNode(const llvm::Value* val = NULL);
	NodeIndex createReturnNode(const llvm::Function* f);
	NodeIndex createVarargNode(const llvm::Function* f);

	// Map lookup interfaces (return NULL if value not found)
	NodeIndex getValueNodeFor(const llvm::Value* val);
	NodeIndex getValueNodeForConstant(const llvm::Constant* c);
	NodeIndex getObjectNodeFor(const llvm::Value* val);
	NodeIndex getObjectNodeForConstant(const llvm::Constant* c);
	NodeIndex getReturnNodeFor(const llvm::Function* f);
	NodeIndex getVarargNodeFor(const llvm::Function* f);

	// Pointer arithmetic
	NodeIndex getOffsetObjectNode(NodeIndex n, unsigned offset)
	{
		return n + offset;
	}

	// Special node getters
	NodeIndex getUniversalPtrNode() const { return UniversalPtrIndex; }
	NodeIndex getUniversalObjNode() const { return UniversalObjIndex; }
	NodeIndex getNullPtrNode() const { return NullPtrIndex; }
	NodeIndex getNullObjectNode() const { return NullObjectIndex; }

	// Size getters
	unsigned getNumNodes() const { return nodes.size(); }

	// For debugging purpose
	void dumpNode(NodeIndex) const;
	void dumpNodeInfo() const;
};

#endif
