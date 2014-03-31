Andersen's pointer analysis algorithm

This is the field-sensitive analysis branch.

Field-sensitivity makes it difficult to apply cycle detection techniques like HCD because of those offset field. It will be slower than the master branch but will be more precise.

Built with LLVM 3.5 SVN 203470
