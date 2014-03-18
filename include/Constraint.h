#ifndef ANDERSEN_CONSTRAINT_H
#define ANDERSEN_CONSTRAINT_H

#include "Node.h"

#include <cassert>

/// AndersConstraint - Objects of this structure are used to represent the various
/// constraints identified by the algorithm.  The constraints are 'copy',
/// for statements like "A = B", 'load' for statements like "A = *B",
/// 'store' for statements like "*A = B", and AddressOf for statements like
/// A = alloca;  The Offset is applied as *(A + K) = B for stores,
/// A = *(B + K) for loads, and A = B + K for copies.  It is
/// illegal on addressof constraints (because it is statically
/// resolvable to A = &C where C = B + K)
class AndersConstraint {
public:
	enum ConstraintType 
	{
		COPY,
		LOAD,
		STORE,
		ADDR_OF,
	};
private:
	ConstraintType type;
	AndersNode* dest;
	AndersNode* src;
	unsigned offset;
public:
	AndersConstraint(ConstraintType Ty, AndersNode* D, AndersNode* S, unsigned O = 0): type(Ty), dest(D), src(S), offset(O)
	{
		assert((offset == 0 || Ty != ADDR_OF) && "Offset is illegal on addressof constraints");
	}

	const AndersNode* getDest() const { return dest; }
	const AndersNode* getSrc() const { return src; }
	unsigned getOffset() const { return offset; }

	bool operator==(const AndersConstraint &RHS) const
	{
		return RHS.type == type && RHS.dest == dest && RHS.src == src && RHS.offset == offset;
	}

	bool operator!=(const AndersConstraint &RHS) const
	{
		return !(*this == RHS);
	}

	bool operator<(const AndersConstraint &RHS) const
	{
		if (RHS.type != type)
			return RHS.type < type;
		else if (RHS.dest != dest)
			return RHS.dest < dest;
		else if (RHS.src != src)
			return RHS.src < src;
		return RHS.offset < offset;
	}

	/*struct AndersConstraintKeyInfo
	{
		static inline AndersConstraint getEmptyKey() {
			return AndersConstraint(AndersConstraint::COPY, ~0U, ~0U, ~0U);
		}
		static inline AndersConstraint getTombstoneKey() {
			return AndersConstraint(AndersConstraint::COPY, ~0U - 1, ~0U - 1, ~0U - 1);
		}
		static unsigned getHashValue(const AndersConstraint &C) {
			return C.src ^ C.dest ^ C.type ^ C.offset;
		}
		static bool isEqual(const AndersConstraint &LHS,
		                    const AndersConstraint &RHS) {
			return LHS.type == RHS.type && LHS.dest == RHS.dest && LHS.src == RHS.src && LHS.offset == RHS.offset;
		}
	};*/
};

#endif
