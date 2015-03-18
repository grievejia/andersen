#ifndef TCFS_ANDERSEN_AA_H
#define TCFS_ANDERSEN_AA_H

#include "Andersen.h"

#include "llvm/Pass.h"
#include "llvm/Analysis/AliasAnalysis.h"

class AndersenAA: public llvm::ModulePass, public llvm::AliasAnalysis
{
private:
	Andersen* anders;
	const llvm::DataLayout* dataLayout;

	AliasResult andersenAlias(const llvm::Value* l1, const llvm::Value* l2);
public:
	static char ID;

	// Interfaces of AliasAnalysis.
	AliasResult alias(const Location& l1, const Location& l2) override;
	void deleteValue(llvm::Value* v) override;
	void copyValue(llvm::Value* from, llvm::Value* to) override;
	bool pointsToConstantMemory(const Location& loc, bool orLocal) override;
	//ModRefResult getModRefInfo (llvm::ImmutableCallSite cs, const Location &loc);

	AndersenAA(): ModulePass(ID), anders(nullptr), dataLayout(nullptr) {}
	bool runOnModule(llvm::Module &M) override;
	void getAnalysisUsage(llvm::AnalysisUsage &AU) const override;
	void* getAdjustedAnalysisPointer(llvm::AnalysisID PI) override;
};

#endif
