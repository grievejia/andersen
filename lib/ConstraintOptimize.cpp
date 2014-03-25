#include "Andersen.h"

#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/DenseSet.h"
#include "llvm/ADT/SparseBitVector.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/Support/ToolOutputFile.h"

#include <deque>
#include <unordered_map>

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

// The technique used here is described in "Exploiting Pointer and Location Equivalence to Optimize Pointer Analysis. In the 14th International Static Analysis Symposium (SAS), August 2007." It is known as the "HVN" algorithm, and is equivalent to value numbering the collapsed constraint graph without evaluating unions. This is used as a pre-pass to HU in order to resolve first order pointer dereferences and speed up/reduce memory usage of HU. Running both is equivalent to HRU without the iteration
// Since there are just too much bookkeeping info during HVN, we wrap all the logic of HVN into a single class here.
class HVNOptimizer
{
private:
	std::vector<AndersConstraint>& constraints;
	AndersNodeFactory& nodeFactory;

	// The predecessor graph
	DenseMap<NodeIndex, SparseBitVector<>> predGraph;
	// Nodes that must be treated conservatively (i.e. never merge with others)
	// Note that REF nodes and ADR nodes are all automatically indirect nodes. This set only keep track of indirect nodes that are not REF or ADR
	DenseSet<NodeIndex> indirectNodes;
	// The SCC stack
	std::stack<unsigned> sccStack;
	// Map from NodeIndex to DFS number, and negative DFS number means never visited
	std::vector<int> dfsNum;
	// The "inComponent" array in Nutilla's improved SCC algorithm
	std::deque<bool> inComponent;
	// Map from NodeIndex to Pointer Equivalence Class
	std::vector<unsigned> peLabel;
	// Map from a set of NodeIndex to Pointer Equivalence Class
	std::unordered_map<SparseBitVector<>, unsigned, SparseBitVectorHash, SparseBitVectorKeyEqual> setLabel;
	// Store the "representative" (or "leader") when there is a merge
	std::vector<NodeIndex> mergeTarget;
	// DFS timestamp
	int timestamp;
	// Current pointer equivalence class number
	unsigned pointerEqClass;


	// During variable substitution, we create unknowns to represent the unknown value that is a dereference of a variable.  These nodes are known as "ref" nodes (since they represent the value of dereferences)
	// Return the node index of the "ref node" (used to represent *n) of n. 
	// We won't actually create that ref node. We cannot use the NodeIndex of that refNode to index into nodeFactory
	NodeIndex getRefNodeIndex(NodeIndex n) const
	{
		return n + nodeFactory.getNumNodes();
	}

	// Return the node index of the "adr node" (used to represent &n) of n. Only addr-taken vars can be adr-ed
	// We won't actually create that adr node. We cannot use the NodeIndex of that adrNode to index into nodeFactory
	NodeIndex getAdrNodeIndex(NodeIndex n) const
	{
		assert(nodeFactory.isObjectNode(n) && "ADR node of a top-level var does not make sense!");
		return n + 2 * nodeFactory.getNumNodes();
	}

	void buildPredecessorGraph()
	{
		for (auto const& c: constraints)
		{
			switch (c.getType())
			{
				case AndersConstraint::ADDR_OF:
				{
					indirectNodes.insert(c.getSrc());
					// Dest = &src edge
					predGraph[c.getDest()].set(getAdrNodeIndex(c.getSrc()));
					// *Dest = src edge
					predGraph[getRefNodeIndex(c.getDest())].set(c.getSrc());
					break;
				}
				case AndersConstraint::LOAD:
				{
					// dest = *src edge
					if (c.getOffset() == 0)
						predGraph[c.getDest()].set(getRefNodeIndex(c.getSrc()));
					else
						indirectNodes.insert(c.getDest());
				}
				case AndersConstraint::STORE:
				{
					// *dest = src edge
					if (c.getOffset() == 0)
						predGraph[getRefNodeIndex(c.getDest())].set(c.getSrc());
				}
				case AndersConstraint::COPY:
				{
					if (c.getOffset() == 0)
					{
						// Dest = Src edge
						predGraph[c.getDest()].set(c.getSrc());
						// *Dest = *Src edge
						predGraph[getRefNodeIndex(c.getDest())].set(getRefNodeIndex(c.getSrc()));
					}
					else
						indirectNodes.insert(c.getDest());
				}
			}
		}
	}

public:
	HVNOptimizer(std::vector<AndersConstraint>& c, AndersNodeFactory& n): constraints(c), nodeFactory(n), dfsNum(n.getNumNodes() * 3, -1), inComponent(n.getNumNodes() * 3), peLabel(n.getNumNodes() * 3), mergeTarget(n.getNumNodes() * 3), timestamp(0), pointerEqClass(1)
	{
		for (unsigned i = 0, e = mergeTarget.size(); i < e; ++i)
			mergeTarget[i] = i;
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
			const SparseBitVector<>& edges = mapping.second;
			for (auto const& idx: edges)
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
			const SparseBitVector<>& edges = mapping.second;
			for (auto const& idx: edges)
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

	void visit(NodeIndex node)
	{
		//errs() << "visiting "; printPredecessorGraphNode(errs(), node); errs() << '\n';
		assert(node < nodeFactory.getNumNodes() * 3 && "Visiting a non-existent node!");
		int myTimeStamp = timestamp++;
		dfsNum[node] = myTimeStamp;

		// Traverse predecessor edges
		auto predItr = predGraph.find(node);
		if (predItr != predGraph.end())
		{
			const SparseBitVector<>& edges = predItr->second;
			for (auto const& pred: edges)
			{
				NodeIndex predRep = mergeTarget[pred];
				if (dfsNum[predRep] < 0)
					visit(predRep);

				if (!inComponent[predRep] && dfsNum[node] > dfsNum[predRep])
					dfsNum[node] = dfsNum[predRep];
			}
		}

		// See if we have any cycle detected
		if (myTimeStamp != dfsNum[node])
		{
			// If not, push the sccStack and go on
			sccStack.push(node);
			return;
		}

		inComponent[node] = true;
		// Merge All nodes in the cycle
		while (!sccStack.empty())
		{
			NodeIndex cycleNode = sccStack.top();
			if (dfsNum[cycleNode] < myTimeStamp)
				break;

			mergeTarget[cycleNode] = node;
			//inComponent[cycleNode] = true;
			if (node < nodeFactory.getNumNodes() && indirectNodes.count(cycleNode))
				indirectNodes.insert(node);

			auto cycleItr = predGraph.find(cycleNode);
			if (cycleItr != predGraph.end())
				predGraph[node] |= (cycleItr->second);

			sccStack.pop();
		}
		
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
		if (predItr != predGraph.end())
		{
			const SparseBitVector<>& edges = predItr->second;
			for (auto const& pred: edges)
			{
				NodeIndex predRep = mergeTarget[pred];
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

	void run()
	{
		// Build a predecessor graph.  This is like our constraint graph with the edges going in the opposite direction, and there are edges for all the constraints, instead of just copy constraints.  We also build implicit edges for constraints are implied but not explicit.  I.E for the constraint a = &b, we add implicit edges *a = b.  This helps us capture more cycles
		buildPredecessorGraph();

		//writePredecessorGraphToFile();
		//dumpPredecessorGraph();

		// Now run Tarjan's SCC algorithm to find cycles, condense predGraph, and explore possible equivalance relations
		for (unsigned i = 0, e = nodeFactory.getNumNodes() * 3; i < e; ++i)
		{
			NodeIndex rep = mergeTarget[i];
			if (dfsNum[rep] < 0)
				visit(rep);
		}

		
	}
};

}	// end of anonymous namespace

// Optimize the constraints by performing offline variable substitution
void Andersen::optimizeConstraints()
{
	// First, let's do HVN
	HVNOptimizer hvn(constraints, nodeFactory);
	hvn.run();
}

