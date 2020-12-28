(skip-build)

(set-cakelisp-option executable-output "bin/cakelisp")

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

(add-build-options "-DUNIX")

(defun-comptime cakelisp-link-hook (manager (& ModuleManager)
                                    linkCommand (& ProcessCommand)
                                    linkTimeInputs (* ProcessCommandInput) numLinkTimeInputs int
                                    &return bool)
  (Log "Cakelisp: Adding link arguments\n")
  ;; Dynamic loading
  (on-call (field linkCommand arguments) push_back
           (array ProcessCommandArgumentType_String
                  "-ldl"))
  ;; Expose Cakelisp symbols for compile-time function symbol resolution
  (on-call (field linkCommand arguments) push_back
           (array ProcessCommandArgumentType_String
                  "-Wl,--export-dynamic"))
  (return true))

(add-compile-time-hook pre-link cakelisp-link-hook)

;; Use separate build configuration in case other things build files from src/
(add-build-config-label "Bootstrap")
