(skip-build)

(set-cakelisp-option executable-output "bin/cakelisp")

(add-c-search-directory-module "src")
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
 "Main.cpp")

(add-build-options "-DMACOS" "-Wall" "-Werror")

;; Cakelisp dynamically loads compile-time code
(add-library-dependency "dl")

;; Use separate build configuration in case other things build files from src/
(add-build-config-label "Bootstrap")
