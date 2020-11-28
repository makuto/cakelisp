(skip-build)
(set-cakelisp-option enable-hot-reloading true)

(import &comptime-only "Macros.cake")

;; High-level explanation:
;; - Find all global and module-local variables
;; - Convert them to pointers so they can be stored on the heap, surviving reloads
;; - Create initializers for these pointers, which are called right after loading/reloading.
;; - Change all references to those variables to automatic pointer dereferencing. This is the expensive part
;; - Create a function for the loader to call to initialize the pointers
(defun-comptime make-code-hot-reloadable (environment (& EvaluatorEnvironment)
                                                      was-code-modified (& bool)
                                                      &return bool)
  (get-or-create-comptime-var stage int 1)
  (get-or-create-comptime-var modules-with-import (<> std::unordered_map std::string int))
  (printf "stage %d\n" (deref stage))
  (when (= (deref stage) 1) ;; Modify variables only once
    (for-in definition-pair (& ObjectDefinitionPair) (field environment definitions)
            (unless (= (field definition-pair second type) ObjectType_Variable)
              (continue))
            (when (= 0 (on-call (field definition-pair first) compare "rooms"))
              (printf "SKIPPING %s\n" (on-call (field definition-pair first) c_str))
              (continue))
            (printf ">>> Variable %s\n" (on-call (field definition-pair first) c_str))
            (var definition (& ObjectDefinition) (field definition-pair second))
            (var modified-var-tokens (* (<> std::vector Token)) (new (<> std::vector Token)))
            (unless (CreateDefinitionCopyMacroExpanded definition (deref modified-var-tokens))
              (delete modified-var-tokens)
              (return false))
            (on-call (field environment comptimeTokens) push_back modified-var-tokens)
            ;; Before
            (prettyPrintTokens (deref modified-var-tokens))

            (var start-token-index int 0)
            (var end-invocation-index int (- (on-call-ptr modified-var-tokens size) 1))
            (var var-name-index int
                 (getExpectedArgument "expected variable name"
                                      (deref modified-var-tokens)
                                      start-token-index 1
	                                  end-invocation-index))
	        (when (= var-name-index -1)
		      (return false))

	        (var type-index int
                 (getExpectedArgument "expected variable type"
                                      (deref modified-var-tokens)
                                      start-token-index 2
	                                  end-invocation-index))
	        (when (= type-index -1)
	          (return false))

            (var var-invocation (& Token) (at 1 (deref modified-var-tokens)))
            (var var-name (& Token) (at var-name-index (deref modified-var-tokens)))
            (var type-start (& Token) (at type-index (deref modified-var-tokens)))

            ;; Pointerify, remove intializer
            (var new-var-tokens (<> std::vector Token))
            (tokenize-push new-var-tokens ((token-splice-addr var-invocation)
                                           (token-splice-addr var-name)
                                           (* (token-splice-addr type-start))
                                           null))

            ;; After
            (prettyPrintTokens new-var-tokens)

            ;; Create intiailizer function
            (var init-function-name Token var-name)
            (var string-var-name Token var-name)
            (set (field string-var-name type) TokenType_String)
            (scope ;; Create initializer function name from variable name
             (var converted-name-buffer ([] 64 char) (array 0))
             ;; TODO: Need to pass this in somehow
             (var name-style NameStyleSettings)
             (lispNameStyleToCNameStyle (field name-style variableNameMode) (on-call (field var-name contents) c_str)
                                        converted-name-buffer (sizeof converted-name-buffer) var-name)

             (var init-function-name-buffer ([] 256 char) (array 0))
             (PrintfBuffer init-function-name-buffer "%sInitialize" converted-name-buffer)
             (set (field init-function-name contents) init-function-name-buffer))

            (var assignment-tokens (<> std::vector Token))
            (scope ;; Optional assignment
             (var assignment-index int
                  (getArgument (deref modified-var-tokens) start-token-index 3 endInvocationIndex))
             (when (!= assignment-index -1)
               (var assignment-token (* Token) (addr (at assignment-index (deref modified-var-tokens))))
               (tokenize-push assignment-tokens
                              (set (deref (token-splice-addr var-name)) (token-splice assignment-token)))))

            (var initializer-procedure-tokens (* (<> std::vector Token)) (new (<> std::vector Token)))
            (on-call (field environment comptimeTokens) push_back initializer-procedure-tokens)
            ;; Note that we don't auto-deref this; this is the only place where that's the case
            (tokenize-push
             (deref initializer-procedure-tokens)
             (defun (token-splice-addr init-function-name) ()
               (var existing-value (* void) nullptr)
               (if (hot-reload-find-variable (token-splice-addr string-var-name) (addr existing-value))
                   (set (token-splice-addr var-name) (type-cast existing-value (* (token-splice-addr type-start))))
                   (block ;; Create the variable
                       ;; C can have an easier time with plain old malloc and cast
                       (set (token-splice-addr var-name) (new (token-splice-addr type-start)))
                     (token-splice-array assignment-tokens)
                     ;; (set (deref (token-splice-addr var-name)) (token-splice-addr assignment))
                     (hot-reload-register-variable (token-splice-addr string-var-name)
                                                   (token-splice-addr var-name))))))
            (prettyPrintTokens (deref initializer-procedure-tokens))

            ;; Make the changes

            ;; Definition references invalid after this!
            (unless (ReplaceAndEvaluateDefinition environment (on-call (field var-name contents) c_str)
                                                  (deref modified-var-tokens))
              (return false))
            (set was-code-modified true)
            (scope ;; Evaluate initializer
             (unless (field definition context module)
               (return false))
             (var initializer-context EvaluatorContext (array))
             (set (field initializer-context module) (field definition context module))
             (set (field initializer-context scope) EvaluatorScope_Module)
             (set (field initializer-context definitionName)
                  (addr (path environment . moduleManager > globalPseudoInvocationName)))
             (set (field initializer-context isRequired) true)

             ;; Make sure HotReloading header is included
             (var module-filename (* (const char)) (path definition . context . module > filename))
             (when (= (on-call (deref modules-with-import) find module-filename)
                        (on-call-ptr modules-with-import end))
               (var import-hot-reloading-tokens (* (<> std::vector Token)) (new (<> std::vector Token)))
               (on-call (field environment comptimeTokens) push_back import-hot-reloading-tokens)
               (tokenize-push (deref import-hot-reloading-tokens) (import "HotReloading.cake"))
               (unless (= 0 (EvaluateGenerate_Recursive
                           environment initializer-context
                           (deref import-hot-reloading-tokens) 0
                           (deref (path definition . context . module > generatedOutput))))
               (return false))
               ;; Meaningless number, only using hash table for fast lookup
               (set (at module-filename (deref modules-with-import)) 1))

             (unless (= 0 (EvaluateGenerate_Recursive
                           environment initializer-context
                           (deref initializer-procedure-tokens) 0
                           (deref (path definition . context . module > generatedOutput))))
               (return false)))))
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
                                                  linkTimeInputs (* ProcessCommandInput)
                                                  numLinkTimeInputs int
                                                  &return bool)
  (command-add-string-argument "-shared")
  ;; (command-add-string-argument "--export-all-symbols")
  (return true))

(add-compile-time-hook pre-link hot-reload-lib-link-hook)

;; TODO: Automatically make library if no main found
(set-cakelisp-option executable-output "libGeneratedCakelisp.so")
