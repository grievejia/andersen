#include "Andersen.h"
#include "CycleDetector.h"
#include "SparseBitVectorGraph.h"

#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/DenseSet.h"
#include "llvm/ADT/SparseBitVector.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/Support/ToolOutputFile.h"

#include <deque>
#include <unordered_map>
#include <set>

using namespace llvm;

namespace {

struct SparseBitVectorHash
{
	std::size_t operator() (const SparseBitVector<>& vec) const
	{
		std::size_t ret = 0;
		for (auto const& idx: vec)
			ret ^= idx;
		return ret;
	}
};

struct SparseBitVectorKeyEqual
{
	bool operator() (const SparseBitVector<>& lhs, const SparseBitVector<>& rhs) const
	{
		return lhs == rhs;
	}
};

// There is something in common in HVN and HU. Put all the shared stuffs in the base class here
class ConstraintOptimizer: public CycleDetector<SparseBitVectorGraph>
{
protected:
	std::vector<AndersConstraint>& constraints;
	AndersNodeFactory& nodeFactory;

	// The predecessor graph
	SparseBitVectorGraph predGraph;
	// Nodes that must be treated conservatively (i.e. never merge with others)
	// Note that REF nodes and ADR nodes are all automatically indirect nodes. This set only keep track of indirect nodes that are not REF or ADR
	DenseSet<NodeIndex> indirectNodes;

	// Map from NodeIndex to Pointer Equivalence Class
	std::vector<unsigned> peLabel;
	// Current pointer equivalence class number
	unsigned pointerEqClass;

	// Store the "representative" (or "leader") when there is a merge in the cycle. Note that this is different from AndersNode::mergeTarget, which will be set AFTER the optimization
	DenseMap<NodeIndex, NodeIndex> mergeTarget;

	// During variable substitution, we create unknowns to represent the unknown value that is a dereference of a variable.  These nodes are known as "ref" nodes (since they represent the value of dereferences)
	// Return the node index of the "ref node" (used to represent *n) of n. 
	// We won't actually create that ref node. We cannot use the NodeIndex of that refNode to index into nodeFactory
	NodeIndex getRefNodeIndex(NodeIndex n) const
	{
		assert(n < nodeFactory.getNumNodes());
		return n + nodeFactory.getNumNodes();
	}

	// Return the node index of the "adr node" (used to represent &n) of n. Only addr-taken vars can be adr-ed
	// We won't actually create that adr node. We cannot use the NodeIndex of that adrNode to index into nodeFactory
	NodeIndex getAdrNodeIndex(NodeIndex n) const
	{
		assert(n < nodeFactory.getNumNodes());
		return n + 2 * nodeFactory.getNumNodes();
	}

	void buildPredecessorGraph()
	{
		for (auto const& c: constraints)
		{
			NodeIndex srcTgt = nodeFactory.getMergeTarget(c.getSrc());
			NodeIndex dstTgt = nodeFactory.getMergeTarget(c.getDest());
			switch (c.getType())
			{
				case AndersConstraint::ADDR_OF:
				{
					indirectNodes.insert(srcTgt);
					// Dest = &src edge
					predGraph.insertEdge(dstTgt, getAdrNodeIndex(srcTgt));
					// *Dest = src edge
					predGraph.insertEdge(getRefNodeIndex(dstTgt), srcTgt);
					break;
				}
				case AndersConstraint::LOAD:
				{
					// dest = *src edge
					predGraph.insertEdge(dstTgt, getRefNodeIndex(srcTgt));
					break;
				}
				case AndersConstraint::STORE:
				{
					// *dest = src edge
					predGraph.insertEdge(getRefNodeIndex(dstTgt), srcTgt);
					break;
				}
				case AndersConstraint::COPY:
				{
					// Dest = Src edge
					predGraph.insertEdge(dstTgt, srcTgt);
					// *Dest = *Src edge
					predGraph.insertEdge(getRefNodeIndex(dstTgt), getRefNodeIndex(srcTgt));
					break;
				}
			}
		}
	}

	void printPredecessorGraphNode(raw_ostream& os, NodeIndex n) const
	{
		if (n >= nodeFactory.getNumNodes() * 2)
			os << "<ADR> ";
		else if (n >= nodeFactory.getNumNodes())
			os << "<REF> ";
		os << "[Node " << n % nodeFactory.getNumNodes() << "]";
	}

	void dumpPredecessorGraph() const
	{
		errs() << "\n----- Predecessor Graph -----\n";
		for (auto const& mapping: predGraph)
		{
			printPredecessorGraphNode(errs(), mapping.first);
			errs()<< "  -->  ";
			const SparseBitVectorGraphNode& sNode = mapping.second;
			for (auto const& idx: sNode)
			{
				printPredecessorGraphNode(errs(), idx);
				errs() << ", ";
			}
			errs() << '\n';
		}
		errs() << "----- End of Print -----\n";
	}

	void writePredecessorGraphToFile() const
	{
		std::string errInfo;
		tool_output_file outFile("dots/pred.dot", errInfo, sys::fs::F_None);
		if (!errInfo.empty())
		{
			errs() << errInfo << '\n';
			return;
		}

		raw_fd_ostream& os = outFile.os();
		os << "digraph G {\n";
		std::deque<bool> hasLabel(nodeFactory.getNumNodes() * 3, false);
		for (auto const& mapping: predGraph)
		{
			if (!hasLabel[mapping.first])
			{
				os << "\tnode" << mapping.first << " [label = \"";
				printPredecessorGraphNode(os, mapping.first);
				os << "\"]\n";
				hasLabel[mapping.first] = true;
			}
			const SparseBitVectorGraphNode& sNode = mapping.second;
			for (auto const& idx: sNode)
			{
				if (!hasLabel[idx])
				{
					os << "\tnode" << idx << " [label = \"";
					printPredecessorGraphNode(os, idx);
					os << "\"]\n";
					hasLabel[idx] = true;
				}
				os << "\tnode" << idx << " -> " << "node" << mapping.first << '\n';
			}
		}
		os << "}\n";

		outFile.keep();
	}

	NodeIndex getMergeTargetRep(NodeIndex idx)
	{
		while (true)
		{
			auto itr = mergeTarget.find(idx);
			if (itr == mergeTarget.end())
				break;
			else
				idx = itr->second;
		}

		return idx;
	}

	NodeType* getRep(NodeIndex idx) override
	{
		return predGraph.getOrInsertNode(getMergeTargetRep(idx));
	}
	// Specify how to process the non-rep nodes if a cycle is found
	void processNodeOnCycle(const NodeType* node, const NodeType* repNode) override
	{
		NodeIndex nodeIdx = node->getNodeIndex();
		NodeIndex repIdx = repNode->getNodeIndex();
		mergeTarget[nodeIdx] = getMergeTargetRep(repIdx);
		if (repIdx < nodeFactory.getNumNodes() && indirectNodes.count(nodeIdx))
			indirectNodes.insert(repIdx);

		predGraph.mergeEdge(repIdx, nodeIdx);
	}

	// Specify how to process the rep nodes if a cycle is found
	void processCycleRepNode(const NodeType* node) override
	{
		propagateLabel(node->getNodeIndex());
	}

	void rewriteConstraint()
	{
		// Since only direct VAR nodes can be assigned non-unique labels, there are only three cases to consider: VAR+VAR, VAR+REF, and VAR+ADR
		// - For VAR+VAR, just merge one node into the other
		// - For VAR+REF, certainly we want to replace the REF node with the VAR node. This will definitely cut down the analysis time because we have one less indirect node to worry about
		// - For VAR+ADR, we want to replace the VAR node with the ADR node because the latter is more straightforward

		std::vector<NodeIndex> revLabelMap(pointerEqClass, AndersNodeFactory::InvalidIndex);
		// Scan all the VAR nodes to see if any of them have the same label as other VAR nodes. We have to perform the merge before constraint rewriting
		for (unsigned i = 0, e = nodeFactory.getNumNodes(); i < e; ++i)
		{
			if (nodeFactory.getMergeTarget(i) != i)
				continue;

			unsigned iLabel = peLabel[i];
			if (revLabelMap[iLabel] == AndersNodeFactory::InvalidIndex)
				revLabelMap[iLabel] = i;
			else if (iLabel != 0)	// We have already found a VAR or ADR node with the same label. Note we must exclude label 0 since it's special and cannot be merged
			{
				//errs() << "MERGE " << i << "with" << revLabelMap[iLabel] << "\n";
				nodeFactory.mergeNode(revLabelMap[iLabel], i);
			}
		}

		// Collect all peLabels that are assigned to ADR nodes
		for (unsigned i = nodeFactory.getNumNodes() * 2, e = nodeFactory.getNumNodes() * 3; i < e; ++i)
		{
			NodeIndex varNode = i - nodeFactory.getNumNodes() * 2;
			if (nodeFactory.getMergeTarget(varNode) != varNode)
				continue;
			revLabelMap[peLabel[i]] = i;
		}

		// Now scan all constraints and see if we can simplify them
		std::vector<AndersConstraint> newConstraints;
		for (auto const& c: constraints)
		{
			// First, if the lhs has label 0 (non-ptr), ignore this constraint
			if (peLabel[c.getDest()] == 0)
				continue;

			// Change the lhs to its mergeTarget
			NodeIndex destTgt = nodeFactory.getMergeTarget(c.getDest());
			// Change the rhs to its merge target
			NodeIndex srcTgt = nodeFactory.getMergeTarget(c.getSrc());
			switch (c.getType())
			{
				case AndersConstraint::ADDR_OF:
				{
					// We don't want to replace src with srcTgt because, after all, the address of a variable is NOT the same as the address of another variable
					newConstraints.emplace_back(AndersConstraint::ADDR_OF, destTgt, c.getSrc());

					break;
				}
				case AndersConstraint::LOAD:
				{
					// If the src is a non-ptr, ignore this constraint
					if (peLabel[srcTgt] == 0)
						break;
					// If the rhs is equivalent to some ADR node, then we are able to replace the load with a copy
					NodeIndex srcTgtTgt = revLabelMap[peLabel[srcTgt]];
					if (srcTgtTgt > nodeFactory.getNumNodes())
					{
						srcTgtTgt %= nodeFactory.getNumNodes();
						//errs() << "REPLACE " << srcTgt << " with &" << srcTgtTgt << "\n";
						if (srcTgtTgt != destTgt)
							newConstraints.emplace_back(AndersConstraint::COPY, destTgt, srcTgtTgt);
					}
					else
					{
						assert(srcTgtTgt == srcTgt);
						newConstraints.emplace_back(AndersConstraint::LOAD, destTgt, srcTgt);
					}

					break;
				}
				case AndersConstraint::STORE:
				{
					// If the lhs is equivalent to some ADR node, then we are able to replace the store with a copy
					NodeIndex destTgtTgt = revLabelMap[peLabel[destTgt]];
					if (destTgtTgt > nodeFactory.getNumNodes())
					{
						destTgtTgt %= nodeFactory.getNumNodes();
						//errs() << "REPLACE " << destTgt << " with &" << destTgtTgt << "\n";
						if (destTgtTgt != srcTgt)
							newConstraints.emplace_back(AndersConstraint::COPY, destTgtTgt, srcTgt);
					}
					else
					{
						assert(destTgtTgt == destTgt);
						newConstraints.emplace_back(AndersConstraint::STORE, destTgt, srcTgt);
					}

					break;
				}
				case AndersConstraint::COPY:
				{
					// Remove useless constraint "A=A"
					if (destTgt == srcTgt)
						break;

					// If the src is a non-ptr, ignore this constraint
					if (peLabel[srcTgt] == 0)
						break;

					// If the rhs is equivalent to some ADR node, then we are able to replace the copy with an addr_of
					NodeIndex srcTgtTgt = revLabelMap[peLabel[srcTgt]];
					if (srcTgtTgt > nodeFactory.getNumNodes())
					{
						srcTgtTgt %= nodeFactory.getNumNodes();
						//errs() << "REPLACE " << srcTgt << " with &" << srcTgtTgt << "\n";
						newConstraints.emplace_back(AndersConstraint::ADDR_OF, destTgt, srcTgtTgt);
					}
					else
					{
						newConstraints.emplace_back(AndersConstraint::COPY, destTgt, srcTgt);
					}

					break;
				}
			}
		}

		// There may be repetitive constraints. Uniquify them
		std::set<AndersConstraint> constraintSet(newConstraints.begin(), newConstraints.end());
		constraints.assign(constraintSet.begin(), constraintSet.end());
	}

	virtual void releaseMemory()
	{
		indirectNodes.clear();
		peLabel.clear();
		mergeTarget.clear();
		predGraph.releaseMemory();
		releaseSCCMemory();
	}

	virtual void propagateLabel(NodeIndex node) = 0;
public:
	ConstraintOptimizer(std::vector<AndersConstraint>& c, AndersNodeFactory& n): constraints(c), nodeFactory(n), peLabel(n.getNumNodes() * 3), pointerEqClass(1)
	{
		// Build a predecessor graph.  This is like our constraint graph with the edges going in the opposite direction, and there are edges for all the constraints, instead of just copy constraints.  We also build implicit edges for constraints are implied but not explicit.  I.E for the constraint a = &b, we add implicit edges *a = b.  This helps us capture more cycles
		buildPredecessorGraph();
	}

	void run() override
	{
		// Now run Tarjan's SCC algorithm to find cycles, condense predGraph, and explore possible equivalance relations
		runOnGraph(&predGraph);

		// For all nodes on the same cycle: assign their representative's pe label to them
		for (auto const& mapping: mergeTarget)
			peLabel[mapping.first] = peLabel[getMergeTargetRep(mapping.second)];

		/*for (unsigned i = 0; i < peLabel.size(); ++i)
		{
			printPredecessorGraphNode(errs(), i);
			errs() << ", peLabel = " << peLabel[i] << "\n";
		}*/

		// We've done labelling. Now rewrite all constraints
		rewriteConstraint();
	}
};

// The technique used here is described in "Exploiting Pointer and Location Equivalence to Optimize Pointer Analysis. In the 14th International Static Analysis Symposium (SAS), August 2007." It is known as the "HVN" algorithm, and is equivalent to value numbering the collapsed constraint graph without evaluating unions. This is used as a pre-pass to HU in order to resolve first order pointer dereferences and speed up/reduce memory usage of HU. Running both is equivalent to HRU without the iteration
// Since there are just too much bookkeeping info during HVN, we wrap all the logic of HVN into a single class here.
class HVNOptimizer: public ConstraintOptimizer
{
private:
	// Map from a set of NodeIndex to Pointer Equivalence Class
	std::unordered_map<SparseBitVector<>, unsigned, SparseBitVectorHash, SparseBitVectorKeyEqual> setLabel;

	void propagateLabel(NodeIndex node) override
	{
		// Indirect node always gets a unique label
		if (node >= nodeFactory.getNumNodes() || indirectNodes.count(node))
		{
			peLabel[node] = pointerEqClass++;
			return;
		}

		// Scan through the predecessor edges and examine what labels they have
		bool allSame = true;
		unsigned lastSeenLabel = 0;
		SparseBitVector<> predLabels;
		const SparseBitVectorGraphNode* sNode = predGraph.getNodeWithIndex(node);
		if (sNode != nullptr)
		{
			for (auto const& pred: *sNode)
			{
				NodeIndex predRep = getMergeTargetRep(pred);
				unsigned predRepLabel = peLabel[predRep];
				// Ignore labels that are equal to us or non-pointers
				if (predRep == node || predRepLabel == 0)
					continue;

				if (lastSeenLabel == 0)
					lastSeenLabel = predRepLabel;
				else if (allSame && predRepLabel != lastSeenLabel)
					allSame = false;

				predLabels.set(predRepLabel);
			}
		}

		// We either have a non-pointer, a copy of an existing node, or a new node. Assign the appropriate pointer equivalence label.
		if (predLabels.empty())
			peLabel[node] = 0;
		else if (allSame)
			peLabel[node] = lastSeenLabel;
		else
		{
			auto labelItr = setLabel.find(predLabels);
			if (labelItr != setLabel.end())
			{
				peLabel[node] = labelItr->second;
			}
			else
			{
				setLabel.insert(std::make_pair(std::move(predLabels), pointerEqClass));
				peLabel[node] = pointerEqClass;
				++pointerEqClass;
			}
		}
	}

public:
	HVNOptimizer(std::vector<AndersConstraint>& c, AndersNodeFactory& n): ConstraintOptimizer(c, n) {}

	void releaseMemory() override
	{
		ConstraintOptimizer::releaseMemory();
		setLabel.clear();
	}
};

// The technique used here is described in "Exploiting Pointer and Location Equivalence to Optimize Pointer Analysis. In the 14th International Static Analysis Symposium (SAS), August 2007."  It is known as the "HU" algorithm, and is equivalent to value numbering the collapsed constraint graph including evaluating unions.
class HUOptimizer: public ConstraintOptimizer
{
private:
	// Map from a set of NodeIndex to Pointer Equivalence Class
	std::unordered_map<SparseBitVector<>, unsigned, SparseBitVectorHash, SparseBitVectorKeyEqual> setLabel;
	// Map from NodeIndex to its offline pts-set
	DenseMap<unsigned, SparseBitVector<>> ptsSet;

	// Try to assign a single label to node. Return true if the assignment succeeds
	bool assignLabel(NodeIndex node)
	{
		// ADR nodes get a unique label and a pts-set that contains the corresponding VAR node
		if (node >= nodeFactory.getNumNodes() * 2)
		{
			peLabel[node] = pointerEqClass++;
			ptsSet[node].set(node - nodeFactory.getNumNodes() * 2);
			return true;
		}

		// REF nodes get a unique label and a pts-set that contains itself (which can never collide with VAR nodes)
		if (node >= nodeFactory.getNumNodes())
		{
			peLabel[node] = pointerEqClass++;
			ptsSet[node].set(node);
			return true;
		}

		// Indirect VAR nodes get a unique label and a pts-set that contains its corresponding ADR node (which can never collide with VAR and REF nodes)
		if (indirectNodes.count(node))
		{
			peLabel[node] = pointerEqClass++;
			ptsSet[node].set(getAdrNodeIndex(node));
			return true;
		}

		return false;
	}

	// Note that ConstraintOptimizer::processNodeOnCycle() is not enough here, because when we merge two nodes we also want the ptsSet of these two nodes to be merged.
	void processNodeOnCycle(const NodeType* node, const NodeType* repNode) override
	{
		ConstraintOptimizer::processNodeOnCycle(node, repNode);
		
		if (!assignLabel(node->getNodeIndex()))
			return;

		auto itr = ptsSet.find(node->getNodeIndex());
		if (itr != ptsSet.end())
			ptsSet[repNode->getNodeIndex()] |= itr->second;
	}

	void propagateLabel(NodeIndex node) override
	{
		if (assignLabel(node))
			return;

		// Direct VAR nodes need more careful examination
		SparseBitVector<>& myPtsSet = ptsSet[node];
		SparseBitVectorGraphNode* sNode = predGraph.getNodeWithIndex(node);
		if (sNode != nullptr)
		{
			for (auto const& pred: *sNode)
			{
				// Be careful! Any insertion to ptsSet here will invalidate myPtsSet
				NodeIndex predRep = getMergeTargetRep(pred);
				auto itr = ptsSet.find(predRep);
				if (itr != ptsSet.end())
					myPtsSet |= (itr->second);
			}
		}
		
		//errs() << "ptsSet [" << node << "] = ";
		//for (auto v: myPtsSet)
		//	errs() << v << ", ";
		//errs() << "\n";

		// If the ptsSet is empty, assign a label of zero
		if (myPtsSet.empty())
			peLabel[node] = 0;
		// Otherwise, see if we have seen this pattern before
		else
		{
			auto labelItr = setLabel.find(myPtsSet);
			if (labelItr != setLabel.end())
			{
				peLabel[node] = labelItr->second;
			}
			else
			{
				setLabel.insert(std::make_pair(myPtsSet, pointerEqClass));
				peLabel[node] = pointerEqClass;
				++pointerEqClass;
			}
		}
	}
public:
	HUOptimizer(std::vector<AndersConstraint>& c, AndersNodeFactory& n): ConstraintOptimizer(c, n) {}

	void releaseMemory() override
	{
		ConstraintOptimizer::releaseMemory();
		ptsSet.clear();
		setLabel.clear();
	}
};

}	// end of anonymous namespace

// Optimize the constraints by performing offline variable substitution
void Andersen::optimizeConstraints()
{
	//errs() << "\n#constraints = " << constraints.size() << "\n";
	dumpConstraints();

	// First, let's do HVN
	// There is an additional assumption here that before HVN, we have not merged any two nodes. Might fix that in the future
	HVNOptimizer hvn(constraints, nodeFactory);
	hvn.run();
	hvn.releaseMemory();

	nodeFactory.dumpRepInfo();
	dumpConstraints();

	//errs() << "#constraints = " << constraints.size() << "\n";

	// Next, do HU
	// There is an additional assumption here that before HU, the predecessor graph will have no cycle. Might fix that in the future
	//HUOptimizer hu(constraints, nodeFactory);
	//hu.run();

	//nodeFactory.dumpRepInfo();
	//dumpConstraints();

	//errs() << "#constraints = " << constraints.size() << "\n";
}

