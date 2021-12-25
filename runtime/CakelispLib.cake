;; An interface for accessing Cakelisp itself at runtime, e.g. for making a program that allows the
;; user to write and build Cakelisp

(add-cpp-build-dependency
 "Tokenizer.cpp"
 "Evaluator.cpp"
 "Utilities.cpp"
 "FileUtilities.cpp"
 "Converters.cpp"
 "Writer.cpp"
 "Generators.cpp"
 "GeneratorHelpers.cpp"
 "RunProcess.cpp"
 "OutputPreambles.cpp"
 "DynamicLoader.cpp"
 "ModuleManager.cpp"
 "Logging.cpp"
 "Build.cpp"
 "Metadata.cpp")

;; TODO: No windows support yet
;; TODO: Reduce duplication with bootstrap and this file

(add-build-options "-DUNIX" "-Wall" "-Werror" "-std=c++11")

;; Cakelisp dynamically loads compile-time code
(add-library-dependency "dl")
;; Compile-time code can call much of Cakelisp. This flag exposes Cakelisp to dynamic libraries
(add-linker-options "--export-dynamic")

