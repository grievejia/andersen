#ifndef ANDERSEN_PTSSET_H
#define ANDERSEN_PTSSET_H

#include "llvm/ADT/SparseBitVector.h"

// We move the points-to set representation here into a separate class
// The intention is to let us try out different internal implementation of this data-structure (e.g. vectors/bitvecs/sets, ref-counted/non-refcounted) easily
class AndersPtsSet
{
private:
	llvm::SparseBitVector<> bitvec;
public:
	typedef llvm::SparseBitVector<>::iterator iterator;

	// Return true if *this has idx as an element
	// This function should be marked const, but we cannot do it because SparseBitVector::test() is not marked const. WHY???
	bool has(unsigned idx)
	{
		return bitvec.test(idx);
	}

	// Return true if the ptsset changes
	bool insert(unsigned idx)
	{
		return bitvec.test_and_set(idx);
	}

	// Return true if *this is a superset of other
	bool contains(const AndersPtsSet& other) const
	{
		return bitvec.contains(other.bitvec);
	}

	// intersectWith: return true if *this and other share points-to elements
	bool intersectWith(const AndersPtsSet& other) const
	{
		return bitvec.intersects(other.bitvec);
	}

	// Return true if the ptsset changes
	bool unionWith(const AndersPtsSet& other)
	{
		return bitvec |= other.bitvec;
	}

	void clear()
	{
		bitvec.clear();
	}

	unsigned getSize() const
	{
		return bitvec.count();		// NOT a constant time operation!
	}
	bool isEmpty() const		// Always prefer using this function to perform empty test 
	{
		return bitvec.empty();
	}

	bool operator==(const AndersPtsSet& other) const
	{
		return bitvec == other.bitvec;
	}

	iterator begin() const { return bitvec.begin(); }
	iterator end() const { return bitvec.end(); }
};

#endif
