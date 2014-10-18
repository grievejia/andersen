#include "AndersenAA.h"

#include "llvm/IR/Module.h"

using namespace llvm;

static inline bool isSetContainingOnly(const AndersPtsSet& set, NodeIndex i)
{
	return (set.getSize() == 1) && (*set.begin() == i);
}

AliasAnalysis::AliasResult AndersenAA::andersenAlias(const llvm::Value* v1, const llvm::Value* v2)
{
	NodeIndex n1 = (anders->nodeFactory).getMergeTarget((anders->nodeFactory).getValueNodeFor(v1));
	NodeIndex n2 = (anders->nodeFactory).getMergeTarget((anders->nodeFactory).getValueNodeFor(v2));

	if (n1 == n2)
		return AliasAnalysis::MustAlias;

	auto itr1 = (anders->ptsGraph).find(n1), itr2 = (anders->ptsGraph).find(n2);
	if (itr1 == (anders->ptsGraph).end() || itr2 == (anders->ptsGraph).end())
		// We knows nothing about at least one of (v1, v2)
		return AliasAnalysis::MayAlias;

	AndersPtsSet& s1 = itr1->second, s2 = itr2->second;
	bool isNull1 = isSetContainingOnly(s1, (anders->nodeFactory).getNullObjectNode());
	bool isNull2 = isSetContainingOnly(s2, (anders->nodeFactory).getNullObjectNode());
	if (isNull1 || isNull2)
		// If any of them is null, we know that they must not alias each other
		return AliasAnalysis::NoAlias;

	if (s1.getSize() == 1 && s2.getSize() == 1 && *s1.begin() == *s2.begin())
		return AliasAnalysis::MustAlias;

	// Compute the intersection of s1 and s2
	for (auto const& idx: s1)
	{
		if (idx == (anders->nodeFactory).getNullObjectNode())
			continue;
		if (s2.has(idx))
			return AliasAnalysis::MayAlias;
	}

	return AliasAnalysis::NoAlias;
}

AliasAnalysis::AliasResult AndersenAA::alias(const AliasAnalysis::Location& l1, const AliasAnalysis::Location& l2)
{
	if (l1.Size == 0 || l2.Size == 0)
		return NoAlias;

	const Value* v1 = (l1.Ptr)->stripPointerCasts();
	const Value* v2 = (l2.Ptr)->stripPointerCasts();

	if (!v1->getType()->isPointerTy() || !v2->getType()->isPointerTy())
		return NoAlias;

	if (v1 == v2)
		return MustAlias;

	AliasResult andersResult = andersenAlias(v1, v2);
	if (andersResult != MayAlias)
		return andersResult;
	else
		return AliasAnalysis::alias(l1, l2);
}

void AndersenAA::deleteValue(llvm::Value* v)
{
	(anders->nodeFactory).removeNodeForValue(v);
}

void AndersenAA::copyValue(llvm::Value* from, llvm::Value* to)
{
	NodeIndex fromNode = (anders->nodeFactory).getValueNodeFor(from);
	if (fromNode == AndersNodeFactory::InvalidIndex)
		return;

	NodeIndex toNode = (anders->nodeFactory).getValueNodeFor(to);
	if (toNode == AndersNodeFactory::InvalidIndex)
		toNode = (anders->nodeFactory).createValueNode(to);

	auto fromItr = (anders->ptsGraph).find(fromNode);
	if (fromItr == (anders->ptsGraph).end())
		return;

	(anders->ptsGraph)[toNode] = fromItr->second;
}

bool AndersenAA::pointsToConstantMemory(const Location& loc, bool orLocal)
{
	NodeIndex node = (anders->nodeFactory).getValueNodeFor(loc.Ptr);
	if (node == AndersNodeFactory::InvalidIndex)
		return AliasAnalysis::pointsToConstantMemory(loc, orLocal);

	auto itr = (anders->ptsGraph).find(node);
	if (itr == (anders->ptsGraph).end())
		// Not a pointer?
		return AliasAnalysis::pointsToConstantMemory(loc, orLocal);

	const AndersPtsSet& ptsSet = itr->second;
	for (auto const& idx: ptsSet)
	{
		if (const Value* val = (anders->nodeFactory).getValueForNode(idx))
		{
			if (!isa<GlobalValue>(val) || (isa<GlobalVariable>(val) && !cast<GlobalVariable>(val)->isConstant()))
        		return AliasAnalysis::pointsToConstantMemory(loc, orLocal);
		}
		else
		{
			if (idx != (anders->nodeFactory).getNullObjectNode())
				return AliasAnalysis::pointsToConstantMemory(loc, orLocal);
		}
	}

	return true;
}

void AndersenAA::getAnalysisUsage(AnalysisUsage &AU) const
{
	AliasAnalysis::getAnalysisUsage(AU);
	AU.addRequired<Andersen>();
	AU.addRequired<DataLayoutPass>();
	AU.setPreservesAll();
}

void* AndersenAA::getAdjustedAnalysisPointer(AnalysisID PI)
{
	if (PI == &AliasAnalysis::ID)
		return (AliasAnalysis *)this;
	return this;
}

bool AndersenAA::runOnModule(Module &M)
{
	InitializeAliasAnalysis(this);

	anders = &getAnalysis<Andersen>();
	dataLayout = &(getAnalysis<DataLayoutPass>().getDataLayout());

	return false;
}

char AndersenAA::ID = 0;
static RegisterPass<AndersenAA> X("anders-aa", "Andersen Alias Analysis", true, true);
static RegisterAnalysisGroup<AliasAnalysis> Y(X);
