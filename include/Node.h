#ifndef ANDERSEN_NODE_H
#define ANDERSEN_NODE_H

#include "llvm/IR/Value.h"

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
protected:
	AndersNode(AndersNodeType t, unsigned i, const llvm::Value* v): type(t), idx(i), value(v) {}
public:
	friend class AndersNodeFactory;
};

// This is the class of node that corresponds to the top-level SSA vars (i.e. llvm::Value*) in the program. Its TCFS counterpart is TopLevelVar
class AndersValueNode: public AndersNode
{
private:
	AndersValueNode(unsigned i, const llvm::Value* v = nullptr): AndersNode(VALUE_NODE, i, v) {}
public:
	friend class AndersNodeFactory;
};

// This is the class of node that corresponds to the memory objects in the program. Its TCFS counterpart is AddrTakenVar
class AndersObjectNode: public AndersNode
{
private:
	AndersObjectNode(unsigned i, const llvm::Value* v = nullptr): AndersNode(OBJ_NODE, i, v) {}
public:
	friend class AndersNodeFactory;
};

#endif
