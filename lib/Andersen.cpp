#include "Andersen.h"

#include "llvm/ADT/Statistic.h"

using namespace llvm;

void Andersen::getAnalysisUsage(AnalysisUsage &AU) const
{
	AU.addRequired<TargetLibraryInfo>();
	AU.setPreservesAll();
}

bool Andersen::runOnModule(Module &M)
{
	tli = &getAnalysis<TargetLibraryInfo>();

	identifyObjects(M);
	collectConstraints(M);
	solveConstraints();

	constraints.clear();

	return false;
}

void Andersen::releaseMemory()
{
}

char Andersen::ID = 0;
static RegisterPass<Andersen> X("anders", "Andersen's inclusion-based points-to analysis", true, true);

