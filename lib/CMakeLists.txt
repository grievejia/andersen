include_directories (${andersen_SOURCE_DIR}/include)

set (AndersenSourceCodes
	Andersen.cpp
	AndersenAA.cpp
	ConstraintCollect.cpp
	ConstraintOptimize.cpp
	ConstraintSolving.cpp
	ExternalLibrary.cpp
	NodeFactory.cpp
)
add_library (AndersenObj OBJECT ${AndersenSourceCodes})
add_library (Andersen SHARED $<TARGET_OBJECTS:AndersenObj>)
add_library (AndersenStatic STATIC $<TARGET_OBJECTS:AndersenObj>)
target_link_libraries (AndersenStatic LLVMCore LLVMSupport)