#include "Andersen.h"

#include "llvm/IR/Module.h"
#include "llvm/IR/CallSite.h"
#include "llvm/Support/raw_ostream.h"

using namespace llvm;

// This function identifies if the external callsite is a library function call, and add constraint correspondingly
// Returns true if the call is properly handled, and false if we fail to recognize the call
bool Andersen::addConstraintForExternalLibrary(ImmutableCallSite cs)
{
	return false;
}

