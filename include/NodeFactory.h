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

	// valueNodeMap - This map indicates the AndersValueNode* that a particular Value* corresponds to
	llvm::DenseMap<const llvm::Value*, AndersValueNode*> valueNodeMap;
	
	/// ObjectNodes - This map contains entries for each memory object in the program: globals, alloca's and mallocs.
	// We are able to represent them as llvm::Value* because we're modeling the heap with the simplest allocation-site approach
	llvm::DenseMap<const llvm::Value*, AndersObjectNode*> objNodeMap;

	/// returnMap - This map contains an entry for each function in the
	/// program that returns a value.
	llvm::DenseMap<const llvm::Function*, AndersValueNode*> returnMap;

	/// varargMap - This map contains the entry used to represent all pointers
	/// passed through the varargs portion of a function call for a particular
	/// function.  An entry is not present in this map for functions that do not
	/// take variable arguments.
	llvm::DenseMap<const llvm::Function*, AndersValueNode*> varargMap;
public:
	AndersNodeFactory();

	AndersValueNode* createValueNode(const llvm::Value* val = NULL);
	AndersObjectNode* createObjectNode(const llvm::Value* val = NULL);
};

#endif
