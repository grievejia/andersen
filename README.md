Andersen's pointer analysis
===============================

Andersen's algorithm is a famous pointer-analysis algorithm proposed in L.O. Andersen's 1994 Ph.D. thesis. The core idea of his algorithm is to translate the input program with statements of the form "p = q" to constraints of the form "q's points-to set is a subset of p's points-to set", hence it is sometimes also referred to as "inclusion-based" algorithm. The analysis is flow-insensitive and context-insensitive, meaning that it just completely ignores the control-flows in the input program and considers that all statements can be executed in arbitrary order.

This project is an implementation of Andersen's analysis in LLVM. The entire algorithm is broken down into three phases:
- Translating from input LLVM IR into a set of constraints.
- Rewriting the constraints into a smaller set of constraints whose solution should be the same as the original set.
- Solving the optimized constraints.

In phase 1, we treats structs in LLVM-IR field-insensitively. This will yield worse result, but the analysis efficiency and correctness can be more easily guaranteed. We plan to move to a field-sensitive implementation in the future, but for now we want to do the quick dirty things first. Dynamic memory allocations are modelled by their allocation site.

In phase 2, two constraint optimization techniques called HVN and HU are used. The basic idea is to search for pointers that have equivalent points-to set and merge together their representations. Details can be found in Ben Hardekopf's SAS'07 paper.

In phase 3, two constraint solving techniques called HCD and LCD are used. The basic idea is to search for strongly-connected-components in the constraint graph on-the-fly. Details can be found in Ben Hardekopf's PLDI'07 paper ("The Ant and the Grasshopper").

Publications
------------

[Program Analysis and Specialization for the C Programming Language](http://www.cs.cornell.edu/courses/cs711/2005fa/papers/andersen-thesis94.pdf). Ph.D. thesis

[Exploiting Pointer and Location Equivalence to Optimize Pointer Analysis](http://www.cs.ucsb.edu/~benh/research/papers/hardekopf07exploiting.pdf). International Static Analysis Symposium (SAS), 2007

[The Ant and the Grasshopper: Fast and Accurate Pointer Analysis for Millions of Lines of Code](http://www.cs.ucsb.edu/~benh/research/papers/hardekopf07ant.pdf). ACM Conference on Programming Language Design and Implementation (PLDI), 2007

Building the project
-----------------

To build Andersen's analysis, you need to have a C++ compiler with C++14 support
installed (e.g. g++ 4.9 or later, clang++ 3.4 or later) as well as cmake 2.8.8 or later. It should compile without trouble on most recent Linux or MacXOS
machines.

1. Making sure that LLVM 3.9 is installed somewhere on your system. Older version of LLVM are guaranteed not to work because of API changes.

2. Checkout this project

3. Build this project
```bash
cd <directory-you-want-to-build-this-project>
cmake <project-source-code-dir> -DCMAKE_BUILD_TYPE=<specify build type (Debug or Release)> -DBUILD_TESTS=<specify whether you want to build test files (ON or OFF)>
make
```
Note that in the configuration step you might want to consider setting the build mode (Release/Debug with or without Asserts) to match the build mode of your LLVM library.

Using Andersen's analysis
----------------

The analysis is implemented as an LLVM pass. By default it does not dump anything into the console, hence the only way you can extract information from it is to write another pass that take the AndersenAA pass as a prerequisite and make alias queries using AndersenAA's public interfaces. AndersenAA conforms to the standard LLVM AliasAnalysis pass, so it shouldn't be too difficult if you know how to use other build-in alias analysis in LLVM (like basicaa).

If you want points-to information rather than alias information, things become trickier. The Andersen pass does have all the points-to information available: check out `Andersen::getPointsToSet()`. Note that memory objects, in our case, are represented by their corresponding allocation site. 

Limitations
----------------

- The analysis does not support the following LLVM instructions: extractvalue, insertvalue, landingpad, resume, atomicrmw, atomiccmpxchg. In other words, exception handling and atomic operations are not considered in my project.

- Field-insensitivity. Adding support for field sensitivity will drastically increase the complexity of the algorithm. 

- External library calls are not completely modelled. Calls to common library functions, such as malloc(), printf(), strcmp(), etc. are properly handled, yet other uncommonly used functions in libc are not. The analysis will dump the name of all external functions not recognized by it to the command line, and if you need the analysis to model them, please look at ExternalLibrary.cpp, or contact me.

Related projects
----------------

Check out my [tpa](https://github.com/grievejia/tpa) repo for a full-blown flow-sensitive, context-sensitive, field-sensitive pointer analysis for LLVM. It hasn't been updated for a while and is woefully undocumented, but I hope it could give up some ideas of how to write such a thing. 

If you only want a more precise alias analysis for your compiler pipeline, I'd recommend using the new [CFLAliasAnalysis](https://github.com/grievejia/GSoC2016) I contributed to in GSoC 2016. The Andersen variant is highly experimental, but it is in-tree and is much less of a hassle to use. 

