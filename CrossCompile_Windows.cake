(set-cakelisp-option executable-output "bin/cakelisp.exe")

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
 "Metadata.cpp"
 "Main.cpp")

(add-build-options "-DWINDOWS" "-DMINGW" "-DCAKELISP_EXPORTING")

(set-cakelisp-option build-time-compiler "/usr/bin/x86_64-w64-mingw32-g++")
(set-cakelisp-option build-time-compile-arguments
                     "-g" "-c" 'source-input "-o" 'object-output
                     'include-search-dirs 'additional-options)

(set-cakelisp-option compile-time-compiler "/usr/bin/x86_64-w64-mingw32-g++")
(set-cakelisp-option compile-time-compile-arguments
                     "-g" "-c" 'source-input "-o" 'object-output
                     'cakelisp-headers-include "-fPIC")

(set-cakelisp-option build-time-linker "/usr/bin/x86_64-w64-mingw32-g++")
(set-cakelisp-option build-time-link-arguments
                     'additional-options "-o" 'executable-output 'object-input
                     'library-search-dirs 'libraries 'library-runtime-search-dirs 'linker-arguments)

;; Use separate build configuration in case other things build files from src/
(add-build-config-label "CrossCompile_Windows")
