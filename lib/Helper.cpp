#include "Helper.h"

#include "llvm/IR/Operator.h"
#include "llvm/IR/Constants.h"
#include "llvm/ADT/SmallVector.h"

using namespace llvm;

// Given a GEP insn or GEP const expr, compute its byte-offset
// The function will resolve nested GEP constexpr, but will not resolve nested GEP instruction
unsigned getGEPOffset(const Value* value, const DataLayout* dataLayout)
{
	// Assume this function always receives GEP value
	const GEPOperator* gepValue = dyn_cast<GEPOperator>(value);
	assert(gepValue != NULL && "getGEPOffset receives a non-gep value!");
	assert(dataLayout != nullptr && "getGEPOffset receives a NULL dataLayout!");

	unsigned offset = 0;
	const Value* baseValue = gepValue->getPointerOperand()->stripPointerCasts();
	// If we have yet another nested GEP const expr, accumulate its offset
	// The reason why we don't accumulate nested GEP instruction's offset is that we aren't required to. Also, it is impossible to do that because we are not sure if the indices of a GEP instruction contains all-consts or not.
	if (const ConstantExpr* cexp = dyn_cast<ConstantExpr>(baseValue))
		if (cexp->getOpcode() == Instruction::GetElementPtr)
			offset += getGEPOffset(cexp, dataLayout);

	//errs() << "gepValue = " << *gepValue << "\n";
	Type* ptrTy = gepValue->getPointerOperand()->getType();
	SmallVector<Value*, 4> indexOps(gepValue->op_begin() + 1, gepValue->op_end());
	// Make sure all indices are constants
	for (unsigned i = 0, e = indexOps.size(); i != e; ++i)
	{
		if (!isa<ConstantInt>(indexOps[i]))
			indexOps[i] = ConstantInt::get(Type::getInt32Ty(ptrTy->getContext()), 0);
	}

	offset += dataLayout->getIndexedOffset(ptrTy, indexOps);

	return offset;
}
