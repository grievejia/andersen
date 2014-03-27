#ifndef STRUCT_ANALYZER_H
#define STRUCT_ANALYZER_H

#include "llvm/IR/Module.h"
#include "llvm/IR/Type.h"
#include "llvm/ADT/iterator_range.h"
#include <vector>
#include <map>

// Every struct type T is mapped to the vectors fieldSize and offsetMap.
// If field [i] in the expanded struct T begins an embedded struct, fieldSize[i] is the # of fields in the largest such struct, else S[i] = 1.
// Also, if a field has index (j) in the original struct, it has index offsetMap[j] in the expanded struct.
class StructInfo
{
private:
	// FIXME: vector<bool> is considered to be BAD C++ practice. We have to switch to something else like deque<bool> some time in the future
	std::vector<bool> arrayFlags;
	std::vector<bool> pointerFlags;
	std::vector<unsigned> fieldSize;
	std::vector<unsigned> offsetMap;

	static const llvm::StructType* maxStruct;
	static unsigned maxStructSize;

	void addOffsetMap(unsigned newOffsetMap) { offsetMap.push_back(newOffsetMap); }
	void addField(unsigned newFieldSize, bool isArray, bool isStruct)
	{
		fieldSize.push_back(newFieldSize);
		arrayFlags.push_back(isArray);
		pointerFlags.push_back(isStruct);
	}
	void appendFields(const StructInfo& other)
	{
		if (!other.isEmpty())
			fieldSize.insert(fieldSize.end(), (other.fieldSize).begin(), (other.fieldSize).end());
		arrayFlags.insert(arrayFlags.end(), (other.arrayFlags).begin(), (other.arrayFlags).end());
		pointerFlags.insert(pointerFlags.end(), (other.pointerFlags).begin(), (other.pointerFlags).end());
	}
	
	// Must be called after all fields have been analyzed
	void finalize()
	{
		assert(fieldSize.size() == arrayFlags.size());
		assert(pointerFlags.size() == arrayFlags.size());
		unsigned numField = fieldSize.size();
		if (numField == 0)
			fieldSize.resize(1);
		fieldSize[0] = numField;
	}

	static void updateMaxStruct(const llvm::StructType* st, unsigned structSize)
	{
		if (structSize > maxStructSize)
		{
			maxStruct = st;
			maxStructSize = structSize;
		}
	}
public:
	typedef std::vector<unsigned>::const_iterator const_iterator;
	unsigned getSize() const { return offsetMap.size(); }
	unsigned getExpandedSize() const { return arrayFlags.size(); }
	
	bool isEmpty() const { return (fieldSize[0] == 0);}
	bool isFieldArray(unsigned field) const { return arrayFlags.at(field); }
	bool isFieldPointer(unsigned field) const { return pointerFlags.at(field); }
	unsigned getOffset(unsigned off) const { return offsetMap.at(off); }

	static unsigned getMaxStructSize() { return maxStructSize; }
	
	friend class StructAnalyzer;
};

// Construct the necessary StructInfo from LLVM IR
// This pass will make GEP instruction handling easier
class StructAnalyzer
{
private:
	// Map llvm type to corresponding StructInfo
	typedef std::map<const llvm::StructType*, StructInfo> StructInfoMap;
	StructInfoMap structInfoMap;

	// Expand (or flatten) the specified StructType and produce StructInfo
	const StructInfo& addStructInfo(const llvm::StructType* st);
	// If st has been calculated before, return its StructInfo; otherwise, calculate StructInfo for st
	const StructInfo& computeStructInfo(const llvm::StructType* st);
public:
	StructAnalyzer() {}

	// Return NULL if info not found
	const StructInfo* getStructInfo(const llvm::StructType* st) const;

	void run(llvm::Module &M);

	void printStructInfo() const;
};

#endif
