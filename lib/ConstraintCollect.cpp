#include "Andersen.h"

#include "llvm/IR/Module.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/InstIterator.h"
#include "llvm/IR/PatternMatch.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/Analysis/ValueTracking.h"
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

	// Next, add any constraints on global variables. Associate the address of the global object as pointing to the memory for the global: &G = <G memory>
	collectConstraintsForGlobals(M);

	// Here is a notable points before we proceed:
	// For functions with non-local linkage type, theoretically we should not trust anything that get passed to it or get returned by it. However, precision will be seriously hurt if we do that because if we do not run a -internalize pass before the -anders pass, almost every function is marked external. We'll just assume that even external linkage will not ruin the analysis result first

	for (auto const& f: M)
	{
		if (f.isDeclaration() || f.isIntrinsic())
			continue;

		// Scan the function body
		// A visitor pattern might help modularity, but it needs more boilerplate codes to set up, and it breaks down the main logic into pieces 

		// First, create a value node for each instructino with pointer type. It is necessary to do the job here rather than on-the-fly because an instruction may refer to the value node definied before it (e.g. phi nodes)
		for (const_inst_iterator itr = inst_begin(f), ite = inst_end(f); itr != ite; ++itr)
		{
			const Instruction* inst = itr.getInstructionIterator();
			if (inst->getType()->isPointerTy())
				nodeFactory.createValueNode(inst);
		}

		// Now, collect constraint for each relevant instruction
		for (const_inst_iterator itr = inst_begin(f), ite = inst_end(f); itr != ite; ++itr)
		{
			const Instruction* inst = itr.getInstructionIterator();
			collectConstraintsForInstruction(inst);
		}
	}
}

void Andersen::collectConstraintsForGlobals(Module& M)
{
	// Create a pointer and an object for each global variable
	for (auto const& globalVal: M.globals())
	{
		NodeIndex gVal = nodeFactory.createValueNode(&globalVal);
		NodeIndex gObj = nodeFactory.createObjectNode(&globalVal);
		constraints.emplace_back(AndersConstraint::ADDR_OF, gVal, gObj);
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

		if (f.isDeclaration() || f.isIntrinsic())
			continue;

		// Create return node
		if (f.getFunctionType()->getReturnType()->isPointerTy())
		{
			nodeFactory.createReturnNode(&f);
		}

		// Create vararg node
		if (f.getFunctionType()->isVarArg())
			nodeFactory.createVarargNode(&f);

		// Add nodes for all formal arguments.
		for (Function::const_arg_iterator itr = f.arg_begin(), ite = f.arg_end(); itr != ite; ++itr)
		{
			if (isa<PointerType>(itr->getType()))
				nodeFactory.createValueNode(itr);
		}
	}

	// Init globals here since an initializer may refer to a global var/func below it
	for (auto const& globalVal: M.globals())
	{
		NodeIndex gObj = nodeFactory.getObjectNodeFor(&globalVal);
		assert(gObj != AndersNodeFactory::InvalidIndex && "Cannot find global object!");

		if (globalVal.hasDefinitiveInitializer())
		{
			addGlobalInitializerConstraints(gObj, globalVal.getInitializer());
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
		// Since we are doing field-insensitive analysis, all objects in the array/struct are pointed-to by the 1st-field pointer
		assert(isa<ConstantArray>(c) || isa<ConstantDataSequential>(c) || isa<ConstantStruct>(c));

		for (unsigned i = 0, e = c->getNumOperands(); i != e; ++i)
			addGlobalInitializerConstraints(objNode, cast<Constant>(c->getOperand(i)));
	}
}

void Andersen::collectConstraintsForInstruction(const Instruction* inst)
{
	switch (inst->getOpcode())
	{
		case Instruction::Alloca:
		{
			NodeIndex valNode = nodeFactory.getValueNodeFor(inst);
			assert(valNode != AndersNodeFactory::InvalidIndex && "Failed to find alloca value node");
			NodeIndex objNode = nodeFactory.createObjectNode(inst);
			constraints.emplace_back(AndersConstraint::ADDR_OF, valNode, objNode);
			break;
		}
		case Instruction::Call:
		case Instruction::Invoke:
		{
			ImmutableCallSite cs(inst);
			assert(cs && "Something wrong with callsite?");

			addConstraintForCall(cs);

			break;
		}
		case Instruction::Ret:
		{
			if (inst->getNumOperands() > 0 && inst->getOperand(0)->getType()->isPointerTy())
			{
				NodeIndex retIndex = nodeFactory.getReturnNodeFor(inst->getParent()->getParent());
				assert(retIndex != AndersNodeFactory::InvalidIndex && "Failed to find return node");
				NodeIndex valIndex = nodeFactory.getValueNodeFor(inst->getOperand(0));
				assert(valIndex != AndersNodeFactory::InvalidIndex && "Failed to find return value node");
				constraints.emplace_back(AndersConstraint::COPY, retIndex, valIndex);
			}
			break;
		}
		case Instruction::Load:
		{
			if (inst->getType()->isPointerTy())
			{
				NodeIndex opIndex = nodeFactory.getValueNodeFor(inst->getOperand(0));
				assert(opIndex != AndersNodeFactory::InvalidIndex && "Failed to find load operand node");
				NodeIndex valIndex = nodeFactory.getValueNodeFor(inst);
				assert(valIndex != AndersNodeFactory::InvalidIndex && "Failed to find load value node");
				constraints.emplace_back(AndersConstraint::LOAD, valIndex, opIndex);
			}
			break;
		}
		case Instruction::Store:
		{
			if (inst->getOperand(0)->getType()->isPointerTy())
			{
				NodeIndex srcIndex = nodeFactory.getValueNodeFor(inst->getOperand(0));
				assert(srcIndex != AndersNodeFactory::InvalidIndex && "Failed to find store src node");
				NodeIndex dstIndex = nodeFactory.getValueNodeFor(inst->getOperand(1));
				assert(dstIndex != AndersNodeFactory::InvalidIndex && "Failed to find store dst node");
				constraints.emplace_back(AndersConstraint::STORE, dstIndex, srcIndex);
			}
			break;
		}
		case Instruction::GetElementPtr:
		{
			assert(inst->getType()->isPointerTy());

			// P1 = getelementptr P2, ... --> <Copy/P1/P2>
			NodeIndex srcIndex = nodeFactory.getValueNodeFor(inst->getOperand(0));
			assert(srcIndex != AndersNodeFactory::InvalidIndex && "Failed to find gep src node");
			NodeIndex dstIndex = nodeFactory.getValueNodeFor(inst);
			assert(dstIndex != AndersNodeFactory::InvalidIndex && "Failed to find gep dst node");

			constraints.emplace_back(AndersConstraint::COPY, dstIndex, srcIndex);

			break;
		}
		case Instruction::PHI:
		{
			if (inst->getType()->isPointerTy())
			{
				const PHINode* phiInst = cast<PHINode>(inst);
				NodeIndex dstIndex = nodeFactory.getValueNodeFor(phiInst);
				assert(dstIndex != AndersNodeFactory::InvalidIndex && "Failed to find phi dst node");
				for (unsigned i = 0, e = phiInst->getNumIncomingValues(); i != e; ++i)
				{
					NodeIndex srcIndex = nodeFactory.getValueNodeFor(phiInst->getIncomingValue(i));
					assert(srcIndex != AndersNodeFactory::InvalidIndex && "Failed to find phi src node");
					constraints.emplace_back(AndersConstraint::COPY, dstIndex, srcIndex);
				}
			}
			break;
		}
		case Instruction::BitCast:
		{
			if (inst->getType()->isPointerTy())
			{
				NodeIndex srcIndex = nodeFactory.getValueNodeFor(inst->getOperand(0));
				assert(srcIndex != AndersNodeFactory::InvalidIndex && "Failed to find bitcast src node");
				NodeIndex dstIndex = nodeFactory.getValueNodeFor(inst);
				assert(dstIndex != AndersNodeFactory::InvalidIndex && "Failed to find bitcast dst node");
				constraints.emplace_back(AndersConstraint::COPY, dstIndex, srcIndex);
			}
			break;
		}
		case Instruction::IntToPtr:
		{
			assert(inst->getType()->isPointerTy());
			
			// Get the node index for dst
			NodeIndex dstIndex = nodeFactory.getValueNodeFor(inst);
			assert(dstIndex != AndersNodeFactory::InvalidIndex && "Failed to find inttoptr dst node");

			// We use pattern matching to look for a matching ptrtoint
			Value* op = inst->getOperand(0);

			// Pointer copy: Y = inttoptr (ptrtoint X)
			Value* srcValue = nullptr;
			if (PatternMatch::match(op, PatternMatch::m_PtrToInt(PatternMatch::m_Value(srcValue))))
			{
				NodeIndex srcIndex = nodeFactory.getValueNodeFor(srcValue);
				assert(srcIndex != AndersNodeFactory::InvalidIndex && "Failed to find inttoptr src node");
				constraints.emplace_back(AndersConstraint::COPY, dstIndex, srcIndex);
				break;
			}
			
			// Pointer arithmetic: Y = inttoptr (ptrtoint (X) + offset)
			if (PatternMatch::match(op,
				PatternMatch::m_Add(
					PatternMatch::m_PtrToInt(
						PatternMatch::m_Value(srcValue)),
					PatternMatch::m_Value())))
			{
				NodeIndex srcIndex = nodeFactory.getValueNodeFor(srcValue);
				assert(srcIndex != AndersNodeFactory::InvalidIndex && "Failed to find inttoptr src node");
				constraints.emplace_back(AndersConstraint::COPY, dstIndex, srcIndex);
				break;
			}
			
			// Otherwise, we really don't know what dst points to
			constraints.emplace_back(AndersConstraint::COPY, dstIndex, nodeFactory.getUniversalPtrNode());

			break;
		}
		case Instruction::Select:
		{
			if (inst->getType()->isPointerTy())
			{
				NodeIndex srcIndex1 = nodeFactory.getValueNodeFor(inst->getOperand(1));
				assert(srcIndex1 != AndersNodeFactory::InvalidIndex && "Failed to find select src node 1");
				NodeIndex srcIndex2 = nodeFactory.getValueNodeFor(inst->getOperand(2));
				assert(srcIndex2 != AndersNodeFactory::InvalidIndex && "Failed to find select src node 2");
				NodeIndex dstIndex = nodeFactory.getValueNodeFor(inst);
				assert(dstIndex != AndersNodeFactory::InvalidIndex && "Failed to find select dst node");
				constraints.emplace_back(AndersConstraint::COPY, dstIndex, srcIndex1);
				constraints.emplace_back(AndersConstraint::COPY, dstIndex, srcIndex2);
			}
			break;
		}
		case Instruction::VAArg:
		{
			if (inst->getType()->isPointerTy())
			{
				NodeIndex dstIndex = nodeFactory.getValueNodeFor(inst);
				assert(dstIndex != AndersNodeFactory::InvalidIndex && "Failed to find va_arg dst node");
				NodeIndex vaIndex = nodeFactory.getVarargNodeFor(inst->getParent()->getParent());
				assert(vaIndex != AndersNodeFactory::InvalidIndex && "Failed to find vararg node");
				constraints.emplace_back(AndersConstraint::COPY, dstIndex, vaIndex);
			}
			break;
		}
		case Instruction::ExtractValue:
		case Instruction::InsertValue:
		{
			if (!inst->getType()->isPointerTy())
				break;
		}
		// We have no intention to support exception-handling in the near future
		case Instruction::LandingPad:
		case Instruction::Resume:
		// Atomic instructions can be modeled by their non-atomic counterparts. To be supported
		case Instruction::AtomicRMW:
		case Instruction::AtomicCmpXchg:
		{
			errs() << *inst << "\n";
			assert(false && "not implemented yet");
		}
		default:
		{
			assert(!inst->getType()->isPointerTy() && "pointer-related inst not handled!");
			break;
		}
	}
}

// There are two types of constraints to add for a function call:
// - ValueNode(callsite) = ReturnNode(call target)
// - ValueNode(formal arg) = ValueNode(actual arg)
void Andersen::addConstraintForCall(ImmutableCallSite cs)
{
	if (const Function* f = cs.getCalledFunction())	// Direct call
	{
		if (f->isDeclaration() || f->isIntrinsic())	// External library call
		{
			// Handle libraries separately
			if (addConstraintForExternalLibrary(cs, f))
				return;
			else	// Unresolved library call: ruin everything!
			{
				errs() << "Unresolved ext function: " << f->getName() << "\n";
				if (cs.getType()->isPointerTy())
				{
					NodeIndex retIndex = nodeFactory.getValueNodeFor(cs.getInstruction());
					assert(retIndex != AndersNodeFactory::InvalidIndex && "Failed to find ret node!");
					constraints.emplace_back(AndersConstraint::COPY, retIndex, nodeFactory.getUniversalPtrNode());
				}
				for (ImmutableCallSite::arg_iterator itr = cs.arg_begin(), ite = cs.arg_end(); itr != ite; ++itr)
				{
					Value* argVal = *itr;
					if (argVal->getType()->isPointerTy())
					{
						NodeIndex argIndex = nodeFactory.getValueNodeFor(argVal);
						assert(argIndex != AndersNodeFactory::InvalidIndex && "Failed to find arg node!");
						constraints.emplace_back(AndersConstraint::COPY, argIndex, nodeFactory.getUniversalPtrNode());
					}
				}
			}
		}
		else	// Non-external function call
		{
			if (cs.getType()->isPointerTy())
			{
				NodeIndex retIndex = nodeFactory.getValueNodeFor(cs.getInstruction());
				assert(retIndex != AndersNodeFactory::InvalidIndex && "Failed to find ret node!");
				//errs() << f->getName() << "\n";
				NodeIndex fRetIndex = nodeFactory.getReturnNodeFor(f);
				assert(fRetIndex != AndersNodeFactory::InvalidIndex && "Failed to find function ret node!");
				constraints.emplace_back(AndersConstraint::COPY, retIndex, fRetIndex);
			}
			// The argument constraints
			addArgumentConstraintForCall(cs, f);
		}
	}
	else	// Indirect call
	{
		// We do the simplest thing here: just assume the returned value can be anything :)
		if (cs.getType()->isPointerTy())
		{
			NodeIndex retIndex = nodeFactory.getValueNodeFor(cs.getInstruction());
			assert(retIndex != AndersNodeFactory::InvalidIndex && "Failed to find ret node!");
			constraints.emplace_back(AndersConstraint::COPY, retIndex, nodeFactory.getUniversalPtrNode());
		}

		// For argument constraints, first search through all addr-taken functions: any function that takes can take as many variables is a potential candidate
		const Module* M = cs.getInstruction()->getParent()->getParent()->getParent();
		for (auto const& f: *M)
		{
			NodeIndex funPtrIndex = nodeFactory.getValueNodeFor(&f);
			if (funPtrIndex == AndersNodeFactory::InvalidIndex)
				// Not an addr-taken function
				continue;

			if (!f.getFunctionType()->isVarArg() && f.arg_size() != cs.arg_size())
				// #arg mismatch
				continue;

			if (f.isDeclaration() || f.isIntrinsic())	// External library call
			{
				if (addConstraintForExternalLibrary(cs, &f))
					continue;
				else
				{
					// Pollute everything
					for (ImmutableCallSite::arg_iterator itr = cs.arg_begin(), ite = cs.arg_end(); itr != ite; ++itr)
					{
						NodeIndex argIndex = nodeFactory.getValueNodeFor(*itr);
						assert(argIndex != AndersNodeFactory::InvalidIndex && "Failed to find arg node!");
						constraints.emplace_back(AndersConstraint::COPY, argIndex, nodeFactory.getUniversalPtrNode());
					}
				}
			}
			else
				addArgumentConstraintForCall(cs, &f);
		}
	}
}

void Andersen::addArgumentConstraintForCall(ImmutableCallSite cs, const Function* f)
{
	Function::const_arg_iterator fItr = f->arg_begin();
	ImmutableCallSite::arg_iterator aItr = cs.arg_begin();
	while (fItr != f->arg_end() && aItr != cs.arg_end())
	{
		const Argument* formal = fItr;
		const Value* actual = *aItr;

		if (formal->getType()->isPointerTy())
		{
			NodeIndex fIndex = nodeFactory.getValueNodeFor(formal);
			assert(fIndex != AndersNodeFactory::InvalidIndex && "Failed to find formal arg node!");
			if (actual->getType()->isPointerTy())
			{
				NodeIndex aIndex = nodeFactory.getValueNodeFor(actual);
				assert(aIndex != AndersNodeFactory::InvalidIndex && "Failed to find actual arg node!");
				constraints.emplace_back(AndersConstraint::COPY, fIndex, aIndex);
			}
			else
				constraints.emplace_back(AndersConstraint::COPY, fIndex, nodeFactory.getUniversalPtrNode());
		}

		++fItr, ++aItr;
	}

	// Copy all pointers passed through the varargs section to the varargs node
	if (f->getFunctionType()->isVarArg())
	{
		while (aItr != cs.arg_end())
		{
			const Value* actual = *aItr;
			if (actual->getType()->isPointerTy())
			{
				NodeIndex aIndex = nodeFactory.getValueNodeFor(actual);
				assert(aIndex != AndersNodeFactory::InvalidIndex && "Failed to find actual arg node!");
				NodeIndex vaIndex = nodeFactory.getVarargNodeFor(f);
				assert(vaIndex != AndersNodeFactory::InvalidIndex && "Failed to find vararg node!");
				constraints.emplace_back(AndersConstraint::COPY, vaIndex, aIndex);
			}

			++aItr;
		}
	}
}
