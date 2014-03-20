#include "StructAnalyzer.h"

#include "llvm/IR/TypeFinder.h"
#include "llvm/Support/raw_ostream.h"

using namespace llvm;

// Initialize max struct info
const StructType* StructInfo::maxStruct = NULL;
unsigned StructInfo::maxStructSize = 0;

const StructInfo& StructAnalyzer::computeStructInfo(const StructType* st)
{
	auto itr = structInfoMap.find(st);
	if (itr != structInfoMap.end())
		return itr->second;
	else
		return addStructInfo(st);
}

const StructInfo& StructAnalyzer::addStructInfo(const StructType* st)
{
	unsigned numField = 0;
	StructInfo& stInfo = structInfoMap[st];

	for (StructType::element_iterator itr = st->element_begin(), ite = st->element_end(); itr != ite; ++itr)
	{
		const Type* subType = *itr;
		bool isArray = isa<ArrayType>(subType);
		// Treat an array field as a single element of its type
		while (const ArrayType* arrayType = dyn_cast<ArrayType>(subType))
			subType = arrayType->getElementType();

		// The offset is where this element will be placed in the expanded struct
		stInfo.addOffsetMap(numField);
		
		// Nested struct
		if (const StructType* structType = dyn_cast<StructType>(subType))
		{
			const StructInfo& subInfo = computeStructInfo(structType);
			
			// Copy information from this substruct
			stInfo.appendFields(subInfo);
			
			numField += subInfo.getExpandedSize();
		}
		else
		{
			stInfo.addField(1, isArray, subType->isPointerTy());
			++numField;
		}
	}
	
	stInfo.finalize();
	StructInfo::updateMaxStruct(st, numField);
	
	return stInfo;
}

// We adopt the approach proposed by Pearce et al. in the paper "efficient field-sensitive pointer analysis of C"
void StructAnalyzer::run(Module &M)
{
	TypeFinder usedStructTypes;
	usedStructTypes.run(M, false);
	for (TypeFinder::iterator itr = usedStructTypes.begin(), ite = usedStructTypes.end(); itr != ite; ++itr)
	{
		const StructType* st = *itr;
		addStructInfo(st);
	}
}

const StructInfo* StructAnalyzer::getStructInfo(const StructType* st) const
{
	auto itr = structInfoMap.find(st);
	if (itr == structInfoMap.end())
		return nullptr;
	else
		return &(itr->second);
}

void StructAnalyzer::printStructInfo() const
{
	errs() << "----------Print StructInfo------------\n";
	for (auto const& mapping: structInfoMap)
	{
		errs() << "Struct " << mapping.first << ": sz < ";
		const StructInfo& info = mapping.second;
		for (auto sz: info.fieldSize)
			errs() << sz << " ";
		errs() << ">, offset < ";
		for (auto off: info.offsetMap)
			errs() << off << " ";
		errs() << ">\n";
	}
	errs() << "----------End of print------------\n";
}
