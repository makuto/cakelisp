(import "HotReloading.cake")
(c-import "stdio.h")

(defun secret-print ()
  (if false
      (return))
  (printf "I got away\n"))

(defun main (&return int)
  (printf "Hello Hot-reloading!\n")

  (simple-macro)
  (secret-print)

  (def-function-signature reload-entry-point-signature (&return bool))
  (var hot-reload-entry-point-func reload-entry-point-signature null)
  (register-function-pointer (type-cast (addr hot-reload-entry-point-func) (* (* void)))
                             ;; TODO Support name conversion at runtime (conversion requires tokens)
                             "reloadableEntryPoint")

  (unless (do-hot-reload)
    (printf "error: failed to hot-reload\n")
    (return 1))

  (while (hot-reload-entry-point-func)
    (unless (do-hot-reload)
      (printf "error: failed to hot-reload\n")
      (return 1)))
  (return 0))

;;
;; Code modification tests
;;

(defun-comptime compile-time-call-before (&return int)
    (return (/ (- 4 2) 2)))

(defmacro simple-macro ()
  (printf "simple-macro: %d, %d!\n" (compile-time-call) (compile-time-call-before))
  (tokenize-push output (printf "Hello, macros!\\n") (magic-number))
  (return true))

(defun-comptime compile-time-call (&return int)
  (return 42))

(defmacro magic-number ()
  (get-or-create-comptime-var test-var std::string)
  (get-or-create-comptime-var test-crazy-var (* (const (* (<> std::vector int)))))
  (set (deref test-var) "Yeah")
  (tokenize-push output (printf "The magic number is 42\\n"))
  (return true))

(defun-comptime sabotage-main-printfs (environment (& EvaluatorEnvironment)
                                                   was-code-modified (& bool)
                                                   &return bool)
  (get-or-create-comptime-var test-var std::string)
  (printf "%s is the message\n" (on-call-ptr test-var c_str))
  (var old-definition-tags (<> std::vector std::string))
  ;; Scope to ensure that definition-it and definition are not referred to after
  ;; ReplaceAndEvaluateDefinition is called, because they will be invalid
  (scope
   (var definition-it (in ObjectDefinitionMap iterator)
        (on-call (field environment definitions) find "main"))
   (when (= definition-it (on-call (field environment definitions) end))
     (printf "sabotage-main-printfs: could not find main!\n")
     (return false))

   (printf "sabotage-main-printfs: found main\n")
   (var definition (& ObjectDefinition) (path definition-it > second))
   (when (!= (FindInContainer (field definition tags) "sabotage-main-printfs-done")
             (on-call (field definition tags) end))
     (printf "sabotage-main-printfs: already modified\n")
     (return true))

   ;; Other modification functions should do this lazily, i.e. only create the expanded definition
   ;; if a modification is necessary
   ;; This must be allocated on the heap because it will be evaluated and needs to stick around
   (var modified-main-tokens (* (<> std::vector Token)) (new (<> std::vector Token)))
   (unless (CreateDefinitionCopyMacroExpanded definition (deref modified-main-tokens))
     (delete modified-main-tokens)
     (return false))

   ;; Environment will handle freeing tokens for us
   (on-call (field environment comptimeTokens) push_back modified-main-tokens)

   ;; Before
   (prettyPrintTokens (deref modified-main-tokens))

   (var prev-token (* Token) null)
   (for-in token (& Token) (deref modified-main-tokens)
           (when (and prev-token
                      (= 0 (on-call (path prev-token > contents) compare "printf"))
                      (ExpectTokenType "sabotage-main-printfs" token TokenType_String))
             (set (field token contents) "I changed your print! Mwahahaha!\\n"))
           (set prev-token (addr token)))

   (printf "sabotage-main-printfs: modified main!\n")
   ;; After
   (prettyPrintTokens (deref modified-main-tokens))

   ;; Copy tags for new definition
   (PushBackAll old-definition-tags (field definition tags))

   ;; Definition references invalid after this!
   (unless (ReplaceAndEvaluateDefinition environment "main" (deref modified-main-tokens))
     (return false))
   (set was-code-modified true))

  ;; Find the new (replacement) definition and add a tag saying it is done replacement
  ;; Note that I also push the tags of the old definition
  (var definition-it (in ObjectDefinitionMap iterator)
        (on-call (field environment definitions) find "main"))
  (when (= definition-it (on-call (field environment definitions) end))
    (printf "sabotage-main-printfs: could not find main after replacement!\n")
    (return false))
  (PushBackAll (path definitionIt > second . tags) old-definition-tags)
  (on-call (path definitionIt > second . tags) push_back "sabotage-main-printfs-done")
  (return true))

(add-compile-time-hook post-references-resolved sabotage-main-printfs)
