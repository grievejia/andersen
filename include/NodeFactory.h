#ifndef ANDERSEN_NODE_FACTORY_H
#define ANDERSEN_NODE_FACTORY_H

#include "Node.h"

#include "llvm/IR/Value.h"
#include "llvm/IR/Function.h"
#include "llvm/ADT/DenseMap.h"

#include <vector>

class AndersNodeFactory
{
public:
	/// This enum defines the GraphNodes indices that correspond to important fixed sets.
	/*enum SpecialNodeType {
		UniversalSet        = 0,
		NullPtr             = 1,
		NullObject          = 2,
		IntNode             = 3,
		AggregateNode       = 4,
		PthreadSpecificNode = 5,
		NumberSpecialNodes
	};*/
private:
	// The set of value nodes 
	std::vector<AndersValueNode> valueNodes;
	// The set of obj nodes
	std::vector<AndersObjectNode> objNodes;

	// Some special indices
	static const unsigned UniversalObjIndex = 0;
	static const unsigned UniversalPtrIndex = 0;
	static const unsigned NullPtrIndex = 1;
	static const unsigned NullObjectIndex = 1;
	static const unsigned IntPtrIndex = 2;

	// valueNodeMap - This map indicates the AndersValueNode* that a particular Value* corresponds to
	llvm::DenseMap<const llvm::Value*, AndersValueNode*> valueNodeMap;
	
	/// ObjectNodes - This map contains entries for each memory object in the program: globals, alloca's and mallocs.
	// We are able to represent them as llvm::Value* because we're modeling the heap with the simplest allocation-site approach
	llvm::DenseMap<const llvm::Value*, AndersObjectNode*> objNodeMap;

	/// returnMap - This map contains an entry for each function in the
	/// program that returns a ptr.
	llvm::DenseMap<const llvm::Function*, AndersValueNode*> returnMap;

	/// varargMap - This map contains the entry used to represent all pointers
	/// passed through the varargs portion of a function call for a particular
	/// function.  An entry is not present in this map for functions that do not
	/// take variable arguments.
	llvm::DenseMap<const llvm::Function*, AndersValueNode*> varargMap;
public:
	AndersNodeFactory();

	// Factory methods
	AndersValueNode* createValueNode(const llvm::Value* val = NULL);
	AndersObjectNode* createObjectNode(const llvm::Value* val = NULL);
	AndersValueNode* createReturnNode(const llvm::Function* f);
	AndersValueNode* createVarargNode(const llvm::Function* f);

	// Map lookup interfaces (return NULL if value not found)
	AndersValueNode* getValueNodeFor(const llvm::Value* val);
	AndersValueNode* getValueNodeForConstant(const llvm::Constant* c);
	AndersObjectNode* getObjectNodeFor(const llvm::Value* val);
	AndersObjectNode* getObjectNodeForConstant(const llvm::Constant* c);
	AndersValueNode* getReturnNodeFor(const llvm::Function* f);
	AndersValueNode* getVarargNodeFor(const llvm::Function* f);

	// Special node getters
	AndersValueNode* getUniversalPtrNode() { return &(valueNodes[UniversalPtrIndex]); }
	AndersObjectNode* getUniversalObjNode() { return &(objNodes[UniversalObjIndex]); }
	AndersValueNode* getNullPtrNode() { return &(valueNodes[NullPtrIndex]); }
	AndersObjectNode* getNullObjectNode() { return &(objNodes[NullObjectIndex]); }
	AndersValueNode* getIntPtrNode() { return &(valueNodes[IntPtrIndex]); }

	// Size getters
	unsigned getNumValueNode() const { return valueNodes.size(); }
	unsigned getNumObjectNode() const { return objNodes.size(); }

	// For debugging purpose
	void dumpNodeInfo() const;
};

#endif
