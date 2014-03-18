#include "PtsSet.h"

#include "llvm/Analysis/CFG.h"
#include "llvm/ADT/OwningPtr.h"
#include "llvm/AsmParser/Parser.h"
#include "llvm/IR/LLVMContext.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/Module.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/IR/InstIterator.h"
#include "llvm/Support/SourceMgr.h"
#include "llvm/Pass.h"
#include "llvm/PassManager.h"
#include "gtest/gtest.h"

using namespace llvm;

namespace {

TEST(AndersTest, PtsSetTest)
{
	AndersPtsSet pSet1, pSet2;
	EXPECT_TRUE(pSet1.isEmpty());
	EXPECT_TRUE(pSet2.isEmpty());

	EXPECT_TRUE(pSet1.insert(5));
	EXPECT_TRUE(pSet2.insert(10));
	EXPECT_TRUE(pSet1.has(5));
	EXPECT_FALSE(pSet1.has(10));
	EXPECT_FALSE(pSet2.has(5));
	EXPECT_TRUE(pSet2.has(10));
	EXPECT_FALSE(pSet1.intersectWith(pSet2));

	EXPECT_TRUE(pSet1.insert(15));
	EXPECT_TRUE(pSet2.insert(15));
	EXPECT_FALSE(pSet2.insert(10));
	EXPECT_TRUE(pSet1.intersectWith(pSet2));

	EXPECT_TRUE(pSet1.unionWith(pSet2));
	EXPECT_TRUE(pSet1.contains(pSet2));
	EXPECT_EQ(pSet1.getSize(), 3u);
}

// This fixture assists in setting up the pass environments
class AndersPassTest: public testing::Test
{
private:
	OwningPtr<Module> M;
protected:
	Module* ParseAssembly(const char *Assembly)
	{
		M.reset(new Module("Module", getGlobalContext()));

		SMDiagnostic Error;
		bool Parsed = ParseAssemblyString(Assembly, M.get(), Error, M->getContext()) == M.get();

		std::string errMsg;
		raw_string_ostream os(errMsg);
		Error.print("", os);

		if (!Parsed) {
		  // A failure here means that the test itself is buggy.
		  report_fatal_error(os.str().c_str());
		}

		return M.get();
	}
};

TEST_F(AndersPassTest, PlaceHolderTest)
{
}

} // end of anonymous namespace
