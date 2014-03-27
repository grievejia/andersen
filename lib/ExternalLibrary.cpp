#include "Andersen.h"

#include "llvm/IR/Module.h"
#include "llvm/IR/CallSite.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/Analysis/ValueTracking.h"
#include "llvm/Support/raw_ostream.h"

#include <cstring>

using namespace llvm;

static const char* noopFuncs[] = {
	"log", "log10", "exp", "exp2", "exp10", "strcmp", "strncmp", "strlen",
	"atoi", "atof",	"atol", "atoll", "remove", "unlink", "rename", "memcmp", "free",
	"execl", "execlp", "execle", "execv", "execvp", "chmod",
	"puts", "write", "open", "create", "truncate", "chdir", "mkdir", "rmdir",
	"read", "pipe",	"wait", "time",	"stat", "fstat", "lstat", "strtod",	"strtof",
	"strtold", "fopen", "fdopen", "fflush", "feof", "fileno", "clearerr", "rewind",
	"ftell", "ferror", "fgetc",	"fgetc", "_IO_getc", "fwrite", "fread",	"fgets",
	"ungetc", "fputc", "fputs", "putc",	"ftell", "rewind", "_IO_putc", "fseek",
	"fgetpos", "fsetpos", "printf", "fprintf", "sprintf", "vprintf", "vfprintf",
	"vsprintf", "scanf", "fscanf", "sscanf", "__assert_fail", "modf", "putchar",
	"isalnum", "isalpha", "isascii", "isatty", "isblank", "iscntrl", "isdigit",
	"isgraph", "islower", "isprint", "ispunct", "isspace", "isupper", "iswalnum",
	"iswalpha", "iswctype", "iswdigit", "iswlower", "iswspace", "iswprint",
	"iswupper", "sin", "cos", "sinf", "cosf", "asin", "acos", "tan", "atan",
	"fabs", "pow", "floor", "ceil", "sqrt", "sqrtf", "hypot", 
	"random", "tolower","toupper", "towlower", "towupper", "system", "clock",
	"exit", "abort", "gettimeofday", "settimeofday",
	"rand", "rand_r", "srand", "seed48", "drand48", "lrand48", "srand48",
	"__isoc99_sscanf", "fclose", "close", "perror", 
	"strerror", // this function returns an extenal static pointer
	"__errno_location", "__ctype_b_loc", "abs", "difftime", "setbuf",
	"_ZdlPv", "_ZdaPv",	// delete and delete[]
	"fesetround", "fegetround", "fetestexcept", "feraiseexcept", "feclearexcept",
	"llvm.bswap.i16", "llvm.bswap.i32", "llvm.ctlz.i64",
	"llvm.lifetime.start", "llvm.lifetime.end", "llvm.stackrestore",
	"memset", "llvm.memset.i32", "llvm.memset.p0i8.i32", "llvm.memset.i64",
	"llvm.memset.p0i8.i64", "llvm.va_end",
	nullptr
};

static const char* mallocFuncs[] = {
	"malloc", "valloc", "calloc",
	"Znwj", "ZnwjRKSt9nothrow_t", "Znwm", "ZnwmRKSt9nothrow_t", 
	"Znaj", "ZnajRKSt9nothrow_t", "Znam", "ZnamRKSt9nothrow_t", 
	"strdup", "strndup",
	"getenv",
	"memalign", "posix_memalign",
	nullptr
};

static const char* reallocFuncs[] = {
	"realloc", "strtok", "strtok_r", "getcwd",
	nullptr
};

static const char* retArg0Funcs[] = {
	"fgets", "gets", "stpcpy",  "strcat", "strchr", "strcpy",
	"strerror_r", "strncat", "strncpy", "strpbrk", "strptime", "strrchr", "strstr",
	nullptr
};

static const char* retArg2Funcs[] = {
	"freopen", nullptr
};

static const char* memcpyFuncs[] = {
	"llvm.memcpy.i32", "llvm.memcpy.p0i8.p0i8.i32", "llvm.memcpy.i64",
	"llvm.memcpy.p0i8.p0i8.i64", "llvm.memmove.i32", "llvm.memmove.p0i8.p0i8.i32",
	"llvm.memmove.i64", "llvm.memmove.p0i8.p0i8.i64",
	"memccpy", "memmove", "bcopy",
	nullptr
};

static const char* convertFuncs[] = {
	"strtod", "strtof", "strtol", "strtold", "strtoll", "strtoul",
	nullptr
};

static bool lookupName(const char* table[], const char* str)
{
	for (unsigned i = 0; table[i] != nullptr; ++i)
	{
		if (strcmp(table[i], str) == 0)
			return true;
	}
	return false;
}

static unsigned traceAllocSize(const Instruction* inst, const StructAnalyzer& structAnalyzer)
{
	assert(inst != NULL);
	const PointerType* pType = cast<PointerType>(inst->getType());
	const Type* elemType = pType->getElementType();
	
	unsigned maxSize = 0;
	while (const ArrayType* arrayType = dyn_cast<ArrayType>(elemType))
		elemType = arrayType->getElementType();
	if (const StructType* stType = dyn_cast<StructType>(elemType))
	{
		const StructInfo* stInfo = structAnalyzer.getStructInfo(stType);
		assert(stInfo != NULL && "structInfoMap should have info for all structs!");
		maxSize = stInfo->getExpandedSize();
	}
	
	// Check all the users of inst
	for (auto const& useInst: inst->uses())
	{
		CastInst* castInst = dyn_cast<CastInst>(useInst);
		if (castInst == NULL)
			continue;
		//Only check casts to other ptr types.
		const PointerType* castType = dyn_cast<PointerType>(castInst->getType());
		if (castType == NULL)
			continue;
		
		const Type* elemType = castType->getElementType();
		while (const ArrayType* arrayType = dyn_cast<ArrayType>(elemType))
			elemType = arrayType->getElementType();
			
		if (const StructType* stType = dyn_cast<StructType>(elemType))
		{
			const StructInfo* stInfo = structAnalyzer.getStructInfo(stType);
			assert(stInfo != NULL && "structInfoMap should have info for all structs!");
			if (stInfo->getExpandedSize() > maxSize)
				maxSize = stInfo->getExpandedSize();
		}
	}
	
	// If the allocation is of non-struct type and we can't find any casts, assume that it may be cast to the largest struct later on.
	if (maxSize == 0)
		return StructInfo::getMaxStructSize();
	else
		return maxSize;
}

// This function identifies if the external callsite is a library function call, and add constraint correspondingly
// If this is a call to a "known" function, add the constraints and return true. If this is a call to an unknown function, return false.
bool Andersen::addConstraintForExternalLibrary(ImmutableCallSite cs, const Function* f)
{
	assert(f != nullptr && "called function is nullptr!");
	assert((f->isDeclaration() || f->isIntrinsic()) && "Not an external function!");

	// These functions don't induce any points-to constraints
	if (lookupName(noopFuncs, f->getName().data()))
		return true;

	// Realloc-like library is a little different: if the first argument is NULL, then it behaves like retArg0Funcs; otherwise, it behaves like mallocFuncs
	bool isReallocLike = lookupName(reallocFuncs, f->getName().data());

	// Library calls that might allocate memory.
	// The complicated issue here is that if the allocation is of struct type, then we have to find the size of that struct, which is often not obvious
	if (lookupName(mallocFuncs, f->getName().data()) || (isReallocLike && !isa<ConstantPointerNull>(cs.getArgument(0))))
	{
		const Instruction* inst = cs.getInstruction();
		unsigned mallocSize = traceAllocSize(inst, structAnalyzer);

		// Create the first heap node
		NodeIndex objIndex = nodeFactory.createObjectNode(inst);
		// Create the rest heap nodes
		for (unsigned i = 1; i < mallocSize; ++i)
			nodeFactory.createObjectNode();

		// Get the pointer node
		NodeIndex ptrIndex = nodeFactory.getValueNodeFor(inst);

		if (ptrIndex == AndersNodeFactory::InvalidIndex)
		{
			// Must be something like posix_memalign()
			if (cs.getCalledFunction()->getName() == "posix_memalign")
			{
				ptrIndex = nodeFactory.getValueNodeFor(cs.getArgument(0));
				assert(ptrIndex != AndersNodeFactory::InvalidIndex && "Failed to find arg0 node");
				constraints.emplace_back(AndersConstraint::STORE, ptrIndex, objIndex);
			}
			else
				assert(false && "unrecognized malloc call");
		}
		else
		{
			// Normal malloc-like call 
			constraints.emplace_back(AndersConstraint::ADDR_OF, ptrIndex, objIndex);
		}
		
		return true;
	}

	if (lookupName(retArg0Funcs, f->getName().data()) || (isReallocLike && isa<ConstantPointerNull>(cs.getArgument(0))))
	{
		NodeIndex retIndex = nodeFactory.getValueNodeFor(cs.getInstruction());
		assert(retIndex != AndersNodeFactory::InvalidIndex && "Failed to find call site node");
		NodeIndex arg0Index = nodeFactory.getValueNodeFor(cs.getArgument(0));
		assert(arg0Index != AndersNodeFactory::InvalidIndex && "Failed to find arg0 node");
		constraints.emplace_back(AndersConstraint::COPY, retIndex, arg0Index);
		return true;
	}

	if (lookupName(retArg2Funcs, f->getName().data()))
	{
		NodeIndex retIndex = nodeFactory.getValueNodeFor(cs.getInstruction());
		assert(retIndex != AndersNodeFactory::InvalidIndex && "Failed to find call site node");
		NodeIndex arg2Index = nodeFactory.getValueNodeFor(cs.getArgument(2));
		assert(arg2Index != AndersNodeFactory::InvalidIndex && "Failed to find arg2 node");
		constraints.emplace_back(AndersConstraint::COPY, retIndex, arg2Index);
		return true;
	}

	if (lookupName(memcpyFuncs, f->getName().data()))
	{
		// This is the most nasty case we can get, since if arg0 and arg1 point to structs of different type, then it is really difficult to make the analysis sound
		NodeIndex arg0Index = nodeFactory.getValueNodeFor(cs.getArgument(0));
		assert(arg0Index != AndersNodeFactory::InvalidIndex && "Failed to find arg0 node");
		NodeIndex arg1Index = nodeFactory.getValueNodeFor(cs.getArgument(1));
		assert(arg1Index != AndersNodeFactory::InvalidIndex && "Failed to find arg1 node");	

		// We need to find out how many fields to copy
		const Value* objVal = GetUnderlyingObject(cs.getArgument(1), dataLayout, 0);
		unsigned size = 1;
		if (objVal->getType()->isStructTy())
		{
			const StructType* stType = cast<StructType>(objVal->getType());
			const StructInfo* stInfo = structAnalyzer.getStructInfo(stType);
			assert(stInfo != NULL && "structInfoMap should have info for all structs!");
			size = stInfo->getExpandedSize();
		}

		// Copy the fields
		for (unsigned i = 0; i < size; ++i)
		{
			NodeIndex tempIndex = nodeFactory.createValueNode();
			constraints.emplace_back(AndersConstraint::LOAD, tempIndex, arg1Index, i);
			constraints.emplace_back(AndersConstraint::STORE, arg1Index, tempIndex, i);
		}

		// Don't forget the return value
		NodeIndex retIndex = nodeFactory.getValueNodeFor(cs.getInstruction());
		if (retIndex != AndersNodeFactory::InvalidIndex)
			constraints.emplace_back(AndersConstraint::COPY, retIndex, arg0Index);

		return true;
	}

	if (lookupName(convertFuncs, f->getName().data()))
	{
		if (!isa<ConstantPointerNull>(cs.getArgument(1)))
		{
			NodeIndex arg0Index = nodeFactory.getValueNodeFor(cs.getArgument(0));
			assert(arg0Index != AndersNodeFactory::InvalidIndex && "Failed to find arg0 node");
			NodeIndex arg1Index = nodeFactory.getValueNodeFor(cs.getArgument(1));
			assert(arg1Index != AndersNodeFactory::InvalidIndex && "Failed to find arg1 node");
			constraints.emplace_back(AndersConstraint::STORE, arg0Index, arg1Index);
		}

		return true;
	}

	if (f->getName() == "llvm.va_start")
	{
		const Instruction* inst = cs.getInstruction();
		const Function* parentF = inst->getParent()->getParent();
		assert(parentF->getFunctionType()->isVarArg());
		NodeIndex arg0Index = nodeFactory.getValueNodeFor(cs.getArgument(0));
		assert(arg0Index != AndersNodeFactory::InvalidIndex && "Failed to find arg0 node");
		NodeIndex vaIndex = nodeFactory.getVarargNodeFor(parentF);
		assert(vaIndex != AndersNodeFactory::InvalidIndex && "Failed to find va node");
		constraints.emplace_back(AndersConstraint::ADDR_OF, arg0Index, vaIndex);

		return true;
	}

	return false;
}

