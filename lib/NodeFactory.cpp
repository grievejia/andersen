#include "NodeFactory.h"

#include "llvm/IR/Constants.h"
#include "llvm/Support/raw_ostream.h"

using namespace llvm;

AndersNodeFactory::AndersNodeFactory()
{
	// Note that we can't use std::vector::emplace_back() here because AndersNode's constructors are private hence std::vector cannot see it

	// Value #0 is always the universal ptr: the ptr that we don't know anything about.
	valueNodes.push_back(AndersValueNode(0));
	// Value #1 always represents the null pointer.
	valueNodes.push_back(AndersValueNode(1));
	// Value #2 represents all pointers casted to int
	valueNodes.push_back(AndersValueNode(2));
	// Object #0 is always the universal obj: the object that we don't know anything about.
	objNodes.push_back(AndersObjectNode(1));
	// Object #1 always represents the null object (the object pointed to by null)
	objNodes.push_back(AndersObjectNode(1));

	assert(valueNodes.size() == 3);
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
	dumpNodeInfo();
	unsigned nextIdx = objNodes.size();
	objNodes.push_back(AndersObjectNode(nextIdx, val));
	AndersObjectNode* retNode = &objNodes[nextIdx];
	if (val != nullptr)
	{
		assert(!objNodeMap.count(val) && "Trying to insert two mappings to revObjNodeMap!");
		objNodeMap[val] = retNode;
	}

	dumpNodeInfo();
	return retNode;
}

AndersValueNode* AndersNodeFactory::createReturnNode(const llvm::Function* f)
{
	unsigned nextIdx = valueNodes.size();
	valueNodes.push_back(AndersValueNode(nextIdx, f));
	AndersValueNode* retNode = &valueNodes[nextIdx];

	assert(!returnMap.count(f) && "Trying to insert two mappings to returnMap!");
	returnMap[f] = retNode;

	return retNode;
}

AndersValueNode* AndersNodeFactory::createVarargNode(const llvm::Function* f)
{
	unsigned nextIdx = valueNodes.size();
	valueNodes.push_back(AndersValueNode(nextIdx, f));
	AndersValueNode* retNode = &valueNodes[nextIdx];

	assert(!varargMap.count(f) && "Trying to insert two mappings to varargMap!");
	varargMap[f] = retNode;

	return retNode;
}

AndersValueNode* AndersNodeFactory::getValueNodeFor(const Value* val)
{
	if (const Constant* c = dyn_cast<Constant>(val))
		if (!isa<GlobalValue>(c))
			return getValueNodeForConstant(c);

	auto itr = valueNodeMap.find(val);
	if (itr == valueNodeMap.end())
		return nullptr;
	else
		return itr->second;
}

AndersValueNode* AndersNodeFactory::getValueNodeForConstant(const llvm::Constant* c)
{
	assert(isa<PointerType>(c->getType()) && "Not a constant pointer!");
	
	if (isa<ConstantPointerNull>(c) || isa<UndefValue>(c))
    	return getNullPtrNode();
    else if (const GlobalValue* gv = dyn_cast<GlobalValue>(c))
    	return getValueNodeFor(gv);
    else if (const ConstantExpr* ce = dyn_cast<ConstantExpr>(c))
    {
		switch (ce->getOpcode())
		{
			case Instruction::GetElementPtr:
				return getValueNodeForConstant(ce->getOperand(0));
			case Instruction::IntToPtr:
				return getUniversalPtrNode();
			case Instruction::PtrToInt:
				return getIntPtrNode();
			case Instruction::BitCast:
				return getValueNodeForConstant(ce->getOperand(0));
			default:
				errs() << "Constant Expr not yet handled: " << *ce << "\n";
				llvm_unreachable(0);
		}
	}

	llvm_unreachable("Unknown constant pointer!");
	return nullptr;
}

AndersObjectNode* AndersNodeFactory::getObjectNodeFor(const Value* val)
{
	if (const Constant* c = dyn_cast<Constant>(val))
		if (!isa<GlobalValue>(c))
			return getObjectNodeForConstant(c);

	auto itr = objNodeMap.find(val);
	if (itr == objNodeMap.end())
		return nullptr;
	else
		return itr->second;
}

AndersObjectNode* AndersNodeFactory::getObjectNodeForConstant(const llvm::Constant* c)
{
	assert(isa<PointerType>(c->getType()) && "Not a constant pointer!");

	if (isa<ConstantPointerNull>(c))
		return getNullObjectNode();
	else if (const GlobalValue* gv = dyn_cast<GlobalValue>(c))
		return getObjectNodeFor(gv);
	else if (const ConstantExpr* ce = dyn_cast<ConstantExpr>(c))
	{
		switch (ce->getOpcode())
		{
			case Instruction::GetElementPtr:
				return getObjectNodeForConstant(ce->getOperand(0));
			case Instruction::IntToPtr:
				return getUniversalObjNode();
			case Instruction::BitCast:
				return getObjectNodeForConstant(ce->getOperand(0));
			default:
				errs() << "Constant Expr not yet handled: " << *ce << "\n";
				llvm_unreachable(0);
		}
	}

	llvm_unreachable("Unknown constant pointer!");
	return nullptr;
}

AndersValueNode* AndersNodeFactory::getReturnNodeFor(const llvm::Function* f)
{
	auto itr = returnMap.find(f);
	if (itr == returnMap.end())
		return nullptr;
	else
		return itr->second;
}

AndersValueNode* AndersNodeFactory::getVarargNodeFor(const llvm::Function* f)
{
	auto itr = varargMap.find(f);
	if (itr == varargMap.end())
		return nullptr;
	else
		return itr->second;
}

void AndersNodeFactory::dumpNodeInfo() const
{
	errs() << "\n----- Print AndersNodeFactory Info -----\n";
	unsigned i = 0;
	for (auto const& vNode: valueNodes)
	{
		errs() << "V #" << i++ << "  idx = " << vNode.getIndex() << ", val = ";
		const Value* val = vNode.getValue();
		if (val == nullptr)
			errs() << "NULL";
		else if (isa<Function>(val))
			errs() << "  <func> " << val->getName();
		else
			errs() << val->getName();
		errs() << "\n";
	}

	i = 0;
	errs() << "\n";
	for (auto const& oNode: objNodes)
	{
		errs() << "O #" << i++ << "  idx = " << oNode.getIndex() << ", val = ";
		if (oNode.getValue() == nullptr)
			errs() << "NULL\n";
		else
			errs() << (oNode.getValue())->getName() << "\n";
	}

	errs() << "\nReturn Map:\n";
	for (auto const& mapping: returnMap)
		errs() << mapping.first->getName() << "  -->>  [ValNode #" << mapping.second->getIndex() << "]\n";
	errs() << "\nVararg Map:\n";
	for (auto const& mapping: varargMap)
		errs() << mapping.first->getName() << "  -->>  [ValNode #" << mapping.second->getIndex() << "]\n";
	errs() << "----- End of Print -----\n";
}
