#ifndef TCFS_ANDERSEN_AA_H
#define TCFS_ANDERSEN_AA_H

#include "Andersen.h"

#include "llvm/Analysis/AliasAnalysis.h"
#include "llvm/Pass.h"

class AndersenAAResult : public llvm::AAResultBase<AndersenAAResult> {
private:
    friend llvm::AAResultBase<AndersenAAResult>;

    Andersen anders;
    llvm::AliasResult andersenAlias(const llvm::Value*, const llvm::Value*);

public:
    AndersenAAResult(const llvm::Module&);

    llvm::AliasResult alias(const llvm::MemoryLocation&,
                            const llvm::MemoryLocation&);
    bool pointsToConstantMemory(const llvm::MemoryLocation&, bool);
};

class AndersenAAWrapperPass : public llvm::ModulePass {
private:
    std::unique_ptr<AndersenAAResult> result;

public:
    static char ID;

    AndersenAAWrapperPass();

    AndersenAAResult& getResult() { return *result; }
    const AndersenAAResult& getResult() const { return *result; }

    bool runOnModule(llvm::Module&) override;
    // bool doFinalization(llvm::Module&) override;
    void getAnalysisUsage(llvm::AnalysisUsage& AU) const override;
};

#endif
