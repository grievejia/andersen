#ifndef ANDERSEN_HELPER_H
#define ANDERSEN_HELPER_H

#include "llvm/IR/Value.h"
#include "llvm/IR/DataLayout.h"

// The file contains prototypes for some general-purpose helper functions
unsigned getGEPOffset(const llvm::Value* value, const llvm::DataLayout* dataLayout);

#endif
