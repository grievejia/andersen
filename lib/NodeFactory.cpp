#include "NodeFactory.h"

using namespace llvm;

AndersNodeFactory::AndersNodeFactory()
{
	// Object #0 is always the universal set: the object that we don't know anything about.
	// Note that we can't use std::vector::emplace_back() here because AndersNode's constructors are private hence std::vector cannot see it
	objNodes.push_back(AndersObjectNode(0));
	
	// Value #0 always represents the null pointer.
	valueNodes.push_back(AndersValueNode(0));
	// Object #1 always represents the null object (the object pointed to by null)
	objNodes.push_back(AndersObjectNode(1));

	assert(valueNodes.size() == 1);
	assert(objNodes.size() == 2);
}

AndersValueNode* AndersNodeFactory::createValueNode(const Value* val)
{
	unsigned nextIdx = valueNodes.size();
	valueNodes.push_back(AndersValueNode(nextIdx, val));
	AndersValueNode* retNode = &valueNodes[nextIdx];
	if (val != nullptr)
	{
		assert(!valueNodeMap.count(val) && "Trying to insert two mappings to revValueNodeMap!");
		valueNodeMap[val] = retNode;
	}
	return retNode;
}

AndersObjectNode* AndersNodeFactory::createObjectNode(const Value* val)
{
	unsigned nextIdx = objNodes.size();
	objNodes.push_back(AndersObjectNode(nextIdx, val));
	AndersObjectNode* retNode = &objNodes[nextIdx];
	if (val != nullptr)
	{
		assert(!objNodeMap.count(val) && "Trying to insert two mappings to revObjNodeMap!");
		objNodeMap[val] = retNode;
	}
	return retNode;
}
