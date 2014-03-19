#include "Andersen.h"

#include "llvm/IR/Module.h"
#include "llvm/IR/Constants.h"

using namespace llvm;

// CollectConstraints - This stage scans the program, adding a constraint to the Constraints list for each instruction in the program that induces a constraint, and setting up the initial points-to graph.

void Andersen::collectConstraints(Module& M)
{
	// First, the universal set points to itself.
	constraints.emplace_back(AndersConstraint::ADDR_OF,
		nodeFactory.getUniversalPtrNode(), nodeFactory.getUniversalObjNode());
	constraints.emplace_back(AndersConstraint::STORE,
		nodeFactory.getUniversalObjNode(), nodeFactory.getUniversalObjNode());

	// Next, the null pointer points to the null object.
	constraints.emplace_back(AndersConstraint::ADDR_OF,
		nodeFactory.getNullPtrNode(), nodeFactory.getNullObjectNode());

	// Next, add any constraints on global variables and their initializers.
	for (auto const& globalVal: M.globals())
	{
		// Associate the address of the global object as pointing to the memory for the global: &G = <G memory>
		AndersValueNode* gVal = nodeFactory.getValueNodeFor(&globalVal);
		assert(gVal != NULL && "global value lookup failed");
		AndersObjectNode* gObj = nodeFactory.getObjectNodeFor(&globalVal);
		assert(gObj != NULL && "global obj lookup failed");
		constraints.emplace_back(AndersConstraint::ADDR_OF, gVal, gObj);

		if (globalVal.hasDefinitiveInitializer())
		{
			addGlobalInitializerConstraints(gObj, globalVal.getInitializer());
			if (isa<PointerType>(globalVal.getType()))
				addConstraintForConstantPointer(&globalVal);
		}
		else
		{
			// If it doesn't have an initializer (i.e. it's defined in another
			// translation unit), it points to the universal set.
			constraints.emplace_back(AndersConstraint::COPY,
				gObj, nodeFactory.getUniversalObjNode());
		}
	}

	for (auto const& f: M)
	{
		// If f is an addr-taken function, handles the function pointer
		if (f.hasAddressTaken())
		{
			AndersValueNode* fVal = nodeFactory.getValueNodeFor(&f);
			assert(fVal != NULL && "funcPtr lookup failed");
			AndersObjectNode* fObj = nodeFactory.getObjectNodeFor(&f);
			assert(fObj != NULL && "funcObj lookup failed");
			constraints.emplace_back(AndersConstraint::ADDR_OF,
				fVal, fObj);
			addConstraintForConstantPointer(&f);
		}
		
		
	}
}

void Andersen::addGlobalInitializerConstraints(AndersObjectNode* objNode, const Constant* c)
{
	if (c->getType()->isSingleValueType())
	{
		if (isa<PointerType>(c->getType()))
			constraints.emplace_back(AndersConstraint::COPY, objNode, nodeFactory.getValueNodeForConstant(c));
	}
	else if (c->isNullValue())
	{
		constraints.emplace_back(AndersConstraint::COPY, objNode, nodeFactory.getNullObjectNode());
	}
	else if (!isa<UndefValue>(c))
	{
		// If this is an array or struct, include constraints for each element.
		assert(isa<ConstantArray>(c) || isa<ConstantStruct>(c) || isa<ConstantDataSequential>(c));
		for (unsigned i = 0, e = c->getNumOperands(); i != e; ++i)
			addGlobalInitializerConstraints(objNode, cast<Constant>(c->getOperand(i)));
	}
}

void Andersen::addConstraintForConstantPointer(const Value* v)
{
	for (auto const& useVal: v->uses())
	{
		if (ConstantExpr* ce = dyn_cast<ConstantExpr>(useVal))
		{
		  if (ce->getOpcode() == Instruction::PtrToInt)
			{
				//errs() << *v << " has been converted to int.\n";
				constraints.emplace_back(AndersConstraint::COPY, nodeFactory.getIntPtrNode(), nodeFactory.getValueNodeFor(v));
				break;
			}
		}
	}
}
