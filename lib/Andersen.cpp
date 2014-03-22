#include "Andersen.h"

#include "llvm/ADT/Statistic.h"
#include "llvm/Support/raw_ostream.h"

using namespace llvm;

const unsigned Andersen::CallReturnPos = 1;
const unsigned Andersen::CallFirstArgPos = 2;

void Andersen::getAnalysisUsage(AnalysisUsage &AU) const
{
	AU.addRequired<TargetLibraryInfo>();
	AU.addRequired<DataLayoutPass>();
	AU.setPreservesAll();
}

bool Andersen::runOnModule(Module &M)
{
	tli = &getAnalysis<TargetLibraryInfo>();
	dataLayout = &(getAnalysis<DataLayoutPass>().getDataLayout());
	nodeFactory.setDataLayout(dataLayout);

	collectConstraints(M);
	nodeFactory.dumpNodeInfo();
	dumpConstraints();
	
	solveConstraints();

	constraints.clear();

	return false;
}

void Andersen::releaseMemory()
{
}

void Andersen::dumpConstraint(const AndersConstraint& item) const
{
	NodeIndex dest = item.getDest();
	NodeIndex src = item.getSrc();
	unsigned offset = item.getOffset();

	switch (item.getType())
	{
		case AndersConstraint::COPY:
		{
			nodeFactory.dumpNode(dest);
			errs() << " = ";
			nodeFactory.dumpNode(src);
			if (offset > 0)
				errs() << " + " << offset;
			break;
		}
		case AndersConstraint::LOAD:
		{
			nodeFactory.dumpNode(dest);
			errs() << " = *(";
			nodeFactory.dumpNode(src);
			if (offset > 0)
				errs() << " + " << offset << ")";
			else
				errs() << ")";
			break;
		}
		case AndersConstraint::STORE:
		{
			errs() << "*";
			nodeFactory.dumpNode(dest);
			errs() << " = (";
			nodeFactory.dumpNode(src);
			if (offset > 0)
				errs() << " + " << offset << ")";
			else
				errs() << ")";
			break;
		}
		case AndersConstraint::ADDR_OF:
		{
			nodeFactory.dumpNode(dest);
			errs() << " = &";
			nodeFactory.dumpNode(src);
		}
	}

	errs() << "\n";
}

void Andersen::dumpConstraints() const
{
	errs() << "\n----- Constraints -----\n";
	for (auto const& item: constraints)
		dumpConstraint(item);
	errs() << "----- End of Print -----\n";
}

char Andersen::ID = 0;
static RegisterPass<Andersen> X("anders", "Andersen's inclusion-based points-to analysis", true, true);

