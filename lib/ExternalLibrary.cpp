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
	"exit", "abort", "gettimeofday", "settimeofday", "sleep", "ctime",
	"strspn", "strcspn", "localtime", "strftime",
	"qsort", "popen", "pclose",
	"rand", "rand_r", "srand", "seed48", "drand48", "lrand48", "srand48",
	"__isoc99_sscanf", "__isoc99_fscanf", "fclose", "close", "perror", 
	"strerror", // this function returns an extenal static pointer
	"__errno_location", "__ctype_b_loc", "abs", "difftime", "setbuf",
	"_ZdlPv", "_ZdaPv",	// delete and delete[]
	"fesetround", "fegetround", "fetestexcept", "feraiseexcept", "feclearexcept",
	"llvm.bswap.i16", "llvm.bswap.i32", "llvm.ctlz.i64",
	"llvm.lifetime.start", "llvm.lifetime.end", "llvm.stackrestore",
	"memset", "llvm.memset.i32", "llvm.memset.p0i8.i32", "llvm.memset.i64",
	"llvm.memset.p0i8.i64", "llvm.va_end",
	// The following functions might not be NOOP. They need to be removed from this list in the future
	"setrlimit", "getrlimit",
	nullptr
};

static const char* mallocFuncs[] = {
	"malloc", "valloc", "calloc",
	"_Znwj", "_ZnwjRKSt9nothrow_t", "_Znwm", "_ZnwmRKSt9nothrow_t", 
	"_Znaj", "_ZnajRKSt9nothrow_t", "_Znam", "_ZnamRKSt9nothrow_t", 
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

static const char* retArg1Funcs[] = {
	// Actually the return value of signal() will NOT alias its second argument, but if you call it twice the return values may alias. We're making conservative assumption here
	"signal",
	nullptr
};

static const char* retArg2Funcs[] = {
	"freopen",
	nullptr
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
	if (lookupName(mallocFuncs, f->getName().data()) || (isReallocLike && !isa<ConstantPointerNull>(cs.getArgument(0))))
	{
		const Instruction* inst = cs.getInstruction();

		// Create the obj node
		NodeIndex objIndex = nodeFactory.createObjectNode(inst);

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

	if (lookupName(retArg1Funcs, f->getName().data()))
	{
		NodeIndex retIndex = nodeFactory.getValueNodeFor(cs.getInstruction());
		assert(retIndex != AndersNodeFactory::InvalidIndex && "Failed to find call site node");
		NodeIndex arg1Index = nodeFactory.getValueNodeFor(cs.getArgument(1));
		assert(arg1Index != AndersNodeFactory::InvalidIndex && "Failed to find arg1 node");
		constraints.emplace_back(AndersConstraint::COPY, retIndex, arg1Index);
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
		NodeIndex arg0Index = nodeFactory.getValueNodeFor(cs.getArgument(0));
		assert(arg0Index != AndersNodeFactory::InvalidIndex && "Failed to find arg0 node");
		NodeIndex arg1Index = nodeFactory.getValueNodeFor(cs.getArgument(1));
		assert(arg1Index != AndersNodeFactory::InvalidIndex && "Failed to find arg1 node");	

		NodeIndex tempIndex = nodeFactory.createValueNode();
		constraints.emplace_back(AndersConstraint::LOAD, tempIndex, arg1Index);
		constraints.emplace_back(AndersConstraint::STORE, arg1Index, tempIndex);

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

