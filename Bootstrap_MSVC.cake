(skip-build)

(set-cakelisp-option executable-output "bin/cakelisp.exe")

(add-c-search-directory module "src")
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

(add-build-options "/DWINDOWS" "/DCAKELISP_EXPORTING" "/EHsc"
                   ;;; DEBUG ONLY!
                   "/Zi" "/FS" "/Fd:bin\cakelisp.pdb" "/DEBUG:FASTLINK")

(set-cakelisp-option build-time-compiler "cl.exe")
(set-cakelisp-option build-time-compile-arguments
                     "/nologo" "/c" 'source-input 'object-output
                     'include-search-dirs 'additional-options)

(set-cakelisp-option compile-time-compiler "cl.exe")
(set-cakelisp-option compile-time-compile-arguments
                     "/nologo" "/c" 'source-input 'object-output
                     'cakelisp-headers-include)
;; "-fPIC"

;; cl.exe for linker also works
(set-cakelisp-option build-time-linker "link.exe")
(set-cakelisp-option build-time-link-arguments
                     "/nologo" 'executable-output 'object-input)

;; Use separate build configuration in case other things build files from src/
(add-build-config-label "Bootstrap_Windows")
