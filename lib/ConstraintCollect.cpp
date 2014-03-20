#include "Andersen.h"

#include "llvm/IR/Module.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/InstIterator.h"
#include "llvm/Support/raw_ostream.h"

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

	// Before we start, collect the struct information in the input program for field-sensitive analysis
	structAnalyzer.run(M);
	nodeFactory.setStructAnalyzer(&structAnalyzer);

	// Next, add any constraints on global variables. Associate the address of the global object as pointing to the memory for the global: &G = <G memory>
	collectConstraintsForGlobals(M);

	// Here is a notable points before we proceed:
	// For functions with non-local linkage type, theoretically we should not trust anything that get passed to it or get returned by it. However, precision will be seriously hurt if we do that because if we do not run a -internalize pass before the -anders pass, almost every function is external. We'll just assume that even external linkage will not ruin the analysis result first
	for (auto const& f: M)
	{
		if (isa<PointerType>(f.getFunctionType()->getReturnType()))
			nodeFactory.createReturnNode(&f);

		if (f.getFunctionType()->isVarArg())
			nodeFactory.createVarargNode(&f);

		if (f.isDeclaration() || f.isIntrinsic())
			continue;

		// Add nodes for all formal arguments.
		for (Function::const_arg_iterator itr = f.arg_begin(), ite = f.arg_end(); itr != ite; ++itr)
		{
			if (isa<PointerType>(itr->getType()))
				nodeFactory.createValueNode(itr);
		}

		// Scan the function body
		// A visitor pattern might help modularity, but it needs more boilerplate codes to set up, and it breaks down the main logic into pieces 
		for (const_inst_iterator itr = inst_begin(f), ite = inst_end(f); itr != ite; ++itr)
		{
			const Instruction* inst = itr.getInstructionIterator();
			if (isa<PointerType>(inst->getType()))
			{
				nodeFactory.createValueNode(inst);
			}
		}
	}
}

void Andersen::collectConstraintsForGlobals(Module& M)
{
	for (auto const& globalVal: M.globals())
	{
		const Type *type = globalVal.getType()->getElementType();
		
		// An array is considered a single variable of its type.
		while(const ArrayType *arrayType= dyn_cast<ArrayType>(type))
			type = arrayType->getElementType();

		// Now construct the pointer and memory object variable
		// It depends on whether the type of this variable is a struct or not
		if (const StructType *structType = dyn_cast<StructType>(type))
		{
			// Construct a stuctVar for the entire variable
			processStruct(&globalVal, structType);
		}
		else
		{
			NodeIndex gVal = nodeFactory.createValueNode(&globalVal);
			NodeIndex gObj = nodeFactory.createObjectNode(&globalVal);
			constraints.emplace_back(AndersConstraint::ADDR_OF, gVal, gObj);
		}
	}

	// Functions and function pointers are also considered global
	for (auto const& f: M)
	{
		// If f is an addr-taken function, create a pointer and an object for it
		if (f.hasAddressTaken())
		{
			NodeIndex fVal = nodeFactory.createValueNode(&f);
			NodeIndex fObj = nodeFactory.createObjectNode(&f);
			constraints.emplace_back(AndersConstraint::ADDR_OF, fVal, fObj);
		}
	}

	// Init globals here since an initializer may refer to a global var/func below it
	for (auto const& globalVal: M.globals())
	{
		NodeIndex gObj = nodeFactory.getObjectNodeFor(&globalVal);
		if (gObj == AndersNodeFactory::InvalidIndex)	// Empty struct
			continue;

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
}

void Andersen::processStruct(const Value* v, const StructType* stType)
{
	// We cannot handle opaque type
	assert(!stType->isOpaque() && "Opaque type not supported");
	// Sanity check
	assert(stType != NULL && "structType is NULL");

	const StructInfo* stInfo = structAnalyzer.getStructInfo(stType);
	assert(stInfo != NULL && "structInfoMap should have info for all structs!");

	// Empty struct has only one pointer that points to nothing
	if (stInfo->isEmpty())
	{
		NodeIndex ptr = nodeFactory.createValueNode(v);
		constraints.emplace_back(AndersConstraint::ADDR_OF, ptr, nodeFactory.getNullObjectNode());
		return;
	}

	// Non-empty structs: create one pointer and one target for each field
	unsigned stSize = stInfo->getExpandedSize();
	// We only need to construct a single top-level variable that points to the starting location. Pointers to locations that follow are not visible on LLVM IR level
	NodeIndex ptr = nodeFactory.createValueNode(v);
	
	// We construct a target variable for each field
	// A better approach is to collect all constant GEP instructions and construct variables only if they are used. We want to do the simplest thing first
	NodeIndex obj = nodeFactory.createObjectNode(v);
	for (unsigned i = 1; i < stSize; ++i)
		nodeFactory.createObjectNode();
	
	constraints.emplace_back(AndersConstraint::ADDR_OF, ptr, obj);
}

void Andersen::addGlobalInitializerConstraints(NodeIndex objNode, const Constant* c)
{
	//errs() << "Called with node# = " << objNode << ", initializer = " << *c << "\n";
	if (c->getType()->isSingleValueType())
	{
		if (isa<PointerType>(c->getType()))
		{
			NodeIndex rhsNode = nodeFactory.getObjectNodeForConstant(c);
			assert(rhsNode != AndersNodeFactory::InvalidIndex && "rhs node not found");
			constraints.emplace_back(AndersConstraint::ADDR_OF, objNode, rhsNode);
		}
	}
	else if (c->isNullValue())
	{
		constraints.emplace_back(AndersConstraint::COPY, objNode, nodeFactory.getNullObjectNode());
	}
	else if (!isa<UndefValue>(c))
	{
		// If this is an array, include constraints for each element.
		if (isa<ConstantArray>(c) || isa<ConstantDataSequential>(c))
		{
			for (unsigned i = 0, e = c->getNumOperands(); i != e; ++i)
				addGlobalInitializerConstraints(objNode, cast<Constant>(c->getOperand(i)));
		}
		else if (isa<ConstantStruct>(c))
		{
			StructType* stType = cast<StructType>(c->getType());
			const StructInfo* stInfo = structAnalyzer.getStructInfo(stType);
			assert(stInfo != NULL && "structInfoMap should have info for all structs!");
			
			// Sequentially initialize each field
			for (unsigned i = 0; i < c->getNumOperands(); ++i)
			{
				NodeIndex field = nodeFactory.getOffsetObjectNode(objNode, i);
				Constant* cv = cast<Constant>(c->getOperand(i));
				addGlobalInitializerConstraints(field, cv);
			}
		}
		else
			llvm_unreachable("Unexpected global initializer");
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
				NodeIndex valNode = nodeFactory.getValueNodeFor(v);
				assert(valNode != AndersNodeFactory::InvalidIndex);
				constraints.emplace_back(AndersConstraint::COPY, nodeFactory.getIntPtrNode(), valNode);
				break;
			}
		}
	}
}
