(skip-build)
(set-cakelisp-option enable-hot-reloading true)

(import &comptime-only "Macros.cake")

;; High-level explanation:
;; - Find all global and module-local variables
;; - Convert them to pointers so they can be stored on the heap, surviving reloads
;; - Create initializers for these pointers, which are called right after loading/reloading
;; - Change all references to those variables to automatic pointer dereferencing. This is the expensive part
;; - Create a function for the loader to call to initialize the pointers
(defun-comptime make-code-hot-reloadable (environment (& EvaluatorEnvironment)
                                                      was-code-modified (& bool)
                                                      &return bool)
  (get-or-create-comptime-var stage int 1)
  (printf "stage %d\n" (deref stage))
  (when (= (deref stage) 1) ;; Modify variables only once
    (for-in definition-pair (& ObjectDefinitionPair) (field environment definitions)
            (unless (= (field definition-pair second type) ObjectType_Variable)
              (continue))
            (printf "Variable %s\n" (on-call (field definition-pair first) c_str))
            (var definition (& ObjectDefinition) (field definition-pair second))
            (var modified-main-tokens (* (<> std::vector Token)) (new (<> std::vector Token)))
            (unless (CreateDefinitionCopyMacroExpanded definition (deref modified-main-tokens))
              (delete modified-main-tokens)
              (return false))
            (on-call (field environment comptimeTokens) push_back modified-main-tokens)
            ;; Before
            (prettyPrintTokens (deref modified-main-tokens))

            (var start-token-index int 0)
            (var end-invocation-index int (- (on-call-ptr modified-main-tokens size) 1))
            (var var-name-index int
                 (getExpectedArgument "expected variable name"
                                      (deref modified-main-tokens)
                                      start-token-index 1
	                                  end-invocation-index))
	        (when (= var-name-index -1)
		      (return false))

	        (var type-index int
                 (getExpectedArgument "expected variable type"
                                      (deref modified-main-tokens)
                                      start-token-index 2
	                                  end-invocation-index))
	        (when (= type-index -1)
	          (return false))

            ;; After
            (prettyPrintTokens (deref modified-main-tokens))))
  (incr (deref stage))
  (return true))

(add-compile-time-hook post-references-resolved make-code-hot-reloadable)

(defmacro command-add-string-argument ()
  (destructure-arguments new-argument-index)
  (quick-token-at new-argument new-argument-index)
  (tokenize-push output (on-call (field linkCommand arguments) push_back
                                 (array ProcessCommandArgumentType_String
                                        (token-splice (addr new-argument)))))
  (return true))

(defun-comptime hot-reload-lib-link-hook (manager (& ModuleManager)
                                                  linkCommand (& ProcessCommand)
                                                  linkTimeInputs (* ProcessCommandInput) numLinkTimeInputs int
                                                  &return bool)
  (command-add-string-argument "-shared")
  ;; (command-add-string-argument "--export-all-symbols")
  (return true))

(add-compile-time-hook pre-link hot-reload-lib-link-hook)

;; TODO: Automatically make library if no main found
(set-cakelisp-option executable-output "libGeneratedCakelisp.so")
