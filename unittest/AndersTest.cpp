#include "PtsSet.h"
#include "SparseBitVectorGraph.h"
#include "NodeFactory.h"

#include "llvm/Analysis/CFG.h"
#include "llvm/AsmParser/Parser.h"
#include "llvm/IR/LLVMContext.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/InstIterator.h"
#include "llvm/IR/PassManager.h"
#include "llvm/Support/SourceMgr.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/Pass.h"
#include "gtest/gtest.h"

#include <memory>

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

TEST(AndersTest, SparseBitVectorGraphTest)
{
	SparseBitVectorGraph graph;

	auto node1 = graph.getOrInsertNode(1);
	auto node2 = graph.getOrInsertNode(2);
	auto node3 = graph.getOrInsertNode(3);
	auto node4 = graph.getOrInsertNode(4);
	auto node5 = graph.getOrInsertNode(5);
	auto node6 = graph.getOrInsertNode(6);

	EXPECT_EQ(graph.getSize(), 6u);
	EXPECT_TRUE(graph.getNodeWithIndex(0) == nullptr);
	ASSERT_TRUE(graph.getNodeWithIndex(1) != nullptr);
	ASSERT_TRUE(graph.getNodeWithIndex(2) != nullptr);
	ASSERT_TRUE(graph.getNodeWithIndex(3) != nullptr);
	ASSERT_TRUE(graph.getNodeWithIndex(4) != nullptr);
	ASSERT_TRUE(graph.getNodeWithIndex(5) != nullptr);
	ASSERT_TRUE(graph.getNodeWithIndex(6) != nullptr);
	EXPECT_TRUE(graph.getNodeWithIndex(7) == nullptr);
	EXPECT_EQ(node1->getNodeIndex(), 1u);
	EXPECT_EQ(node2->getNodeIndex(), 2u);
	EXPECT_EQ(node3->getNodeIndex(), 3u);
	EXPECT_EQ(node4->getNodeIndex(), 4u);
	EXPECT_EQ(node5->getNodeIndex(), 5u);
	EXPECT_EQ(node6->getNodeIndex(), 6u);

	//        |-> 3 \
	// 1 -> 2 |      -> 5 -> 6
	//        |-> 4 /
	graph.insertEdge(1, 2);
	graph.insertEdge(2, 3);
	graph.insertEdge(2, 4);
	graph.insertEdge(3, 5);
	graph.insertEdge(4, 5);
	graph.insertEdge(5, 6);

	EXPECT_EQ(node1->succ_getSize(), 1u);
	EXPECT_EQ(node2->succ_getSize(), 2u);
	EXPECT_EQ(node3->succ_getSize(), 1u);
	EXPECT_EQ(node4->succ_getSize(), 1u);
	EXPECT_EQ(node5->succ_getSize(), 1u);
	EXPECT_EQ(node6->succ_getSize(), 0u);

	graph.mergeEdge(4, 5);
	EXPECT_EQ(node4->succ_getSize(), 2u);
	graph.mergeEdge(5, 6);
	EXPECT_EQ(node5->succ_getSize(), 1u);
	graph.mergeEdge(3, 2);
	EXPECT_EQ(node3->succ_getSize(), 3u);
}

TEST(AndersTest, NodeMergeTest)
{
	AndersNodeFactory factory;

	auto n0 = factory.createValueNode();
	auto n1 = factory.createValueNode();
	auto n2 = factory.createValueNode();
	auto n3 = factory.createValueNode();
	auto n4 = factory.createValueNode();

	factory.mergeNode(n0, n1);
	factory.mergeNode(n2, n3);
	EXPECT_EQ(factory.getMergeTarget(n0), factory.getMergeTarget(n1));
	EXPECT_EQ(factory.getMergeTarget(n2), factory.getMergeTarget(n3));

	factory.mergeNode(n4, n0);
	EXPECT_EQ(factory.getMergeTarget(n4), factory.getMergeTarget(n1));

	factory.mergeNode(n2, n4);
	EXPECT_EQ(factory.getMergeTarget(n1), factory.getMergeTarget(n2));
	EXPECT_EQ(factory.getMergeTarget(n3), factory.getMergeTarget(n4));
}

// This fixture assists in setting up the pass environments
class AndersPassTest: public testing::Test
{
private:
	std::unique_ptr<Module> M;
protected:
	Module* ParseAssembly(const char *Assembly)
	{
		SMDiagnostic Error;
		M = parseAssemblyString(Assembly, Error, getGlobalContext());

		std::string errMsg;
		raw_string_ostream os(errMsg);
		Error.print("", os);

		if (!M) {
		  // A failure here means that the test itself is buggy.
		  report_fatal_error(os.str().c_str());
		}

		return M.get();
	}
};

TEST_F(AndersPassTest, NodeFactoryTest)
{
	auto module = ParseAssembly(
		"define i32 @main() {\n"
		"bb:\n"
		"  %x = alloca i32, align 4\n"
		"  %y = alloca i32, align 4\n"
		"  %z = alloca i32, align 4\n"
		"  %w = alloca i32*, align 8\n"
		"  ret i32 0\n"
		"}\n"
	);

	auto f = module->begin();
	auto bb = f->begin();
	auto itr = bb->begin();
	auto x = &*itr;
	auto y = &*++itr;
	auto z = &*++itr;
	auto w = &*++itr;

	AndersNodeFactory factory;
	auto vx = factory.createValueNode(x);
	auto vy = factory.createValueNode(y);
	auto oz = factory.createObjectNode(z);
	auto ow = factory.createObjectNode(w);

	EXPECT_EQ(x, factory.getValueForNode(vx));
	EXPECT_EQ(y, factory.getValueForNode(vy));
	EXPECT_TRUE(factory.isObjectNode(oz));
	EXPECT_TRUE(factory.isObjectNode(ow));
	EXPECT_EQ(factory.getValueNodeFor(x), vx);
	EXPECT_EQ(factory.getValueNodeFor(y), vy);
	EXPECT_EQ(factory.getValueNodeFor(z), AndersNodeFactory::InvalidIndex);
	EXPECT_EQ(factory.getValueNodeFor(w), AndersNodeFactory::InvalidIndex);
	EXPECT_EQ(factory.getObjectNodeFor(z), oz);
	EXPECT_EQ(factory.getObjectNodeFor(w), ow);
}

} // end of anonymous namespace
