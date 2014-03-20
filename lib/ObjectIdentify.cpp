#include "Andersen.h"

#include "llvm/IR/Module.h"
#include "llvm/IR/CallSite.h"
#include "llvm/IR/InstIterator.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/Support/raw_ostream.h"

using namespace llvm;
/*
// This function performs similar task to llvm::isAllocationFn (without the prototype check). However, the llvm version does not correctly handle functions like memalign and posix_memalign, and that is why we have to re-write it here again...
static bool isMallocCall(ImmutableCallSite cs, const TargetLibraryInfo* tli)
{
	const Function* callee = cs.getCalledFunction();
	if (callee == nullptr || !callee->isDeclaration())
		return false;

	static const LibFunc::Func AllocationFns[] = {
		LibFunc::malloc, LibFunc::valloc, LibFunc::calloc, LibFunc::realloc, LibFunc::reallocf, 
		LibFunc::Znwj, LibFunc::ZnwjRKSt9nothrow_t,
		LibFunc::Znwm, LibFunc::ZnwmRKSt9nothrow_t, 
		LibFunc::Znaj, LibFunc::ZnajRKSt9nothrow_t, 
		LibFunc::Znam, LibFunc::ZnamRKSt9nothrow_t, 
		LibFunc::strdup, LibFunc::strndup,
		LibFunc::memalign, LibFunc::posix_memalign 
	};

	StringRef fName = callee->getName();
	LibFunc::Func tliFunc;
	if (tli == NULL || !tli->getLibFunc(fName, tliFunc))
		return false;

	for (unsigned i = 0, e = array_lengthof(AllocationFns); i < e; ++i)
	{
		if (AllocationFns[i] == tliFunc)
			return true;
	}

	// TODO: check prototype
	return false;
}

// identifyObjects - This stage scans the program, adding an entry to the AndersNodeFactory for each memory object in the program (global, stack or heap)
// Functionally it is very similar to the VariableFactory pass in TCFS

void Andersen::identifyObjects(Module& M)
{
	// Add all the globals first.
	for (auto const& globalValue: M.globals())
	{
		nodeFactory.createValueNode(&globalValue);
		nodeFactory.createObjectNode(&globalValue);
	}

	for (auto const& f: M)
	{
		// If f is an addr-taken function, create a pointer and an object for it
		if (f.hasAddressTaken())
		{
			nodeFactory.createValueNode(&f);
			nodeFactory.createObjectNode(&f);
		}

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
			{
				nodeFactory.createValueNode(itr);
			}
		}

		// Scan the function body, creating a memory object for each heap/stack allocation in the body of the function and a node to represent all pointer values defined by instructions and used as operands.
		for (const_inst_iterator itr = inst_begin(f), ite = inst_end(f); itr != ite; ++itr)
		{
			const Instruction* inst = itr.getInstructionIterator();
			if (isa<PointerType>(inst->getType()))
			{
				nodeFactory.createValueNode(inst);
				// If this is a stack allocation, create a node for the memory object
				if (isa<AllocaInst>(inst))
					nodeFactory.createObjectNode(inst);
			}


			ImmutableCallSite cs(inst);
			if (cs)
			{
				//if this is a malloc-like call, create a node for the heap memory object
				if (isMallocCall(cs, tli))
					nodeFactory.createObjectNode(inst);

				// Calls to inline asm need to be added as well because the callee isn't referenced anywhere else.
				const Value* callValue = cs.getCalledValue();
				if (isa<InlineAsm>(callValue))
					nodeFactory.createObjectNode(inst);
			}
		}
	}
	
}
*/
