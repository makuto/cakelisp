(import "HotReloading.cake")
(c-import "stdio.h")

(defun main (&return int)
  (printf "Hello Hot-reloading!\n")
  (simple-macro)

  (def-function-signature reload-entry-point-signature (&return bool))
  (var hot-reload-entry-point-func reload-entry-point-signature nullptr)
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

(defun-comptime build-text-adventure ()
  (printf "Hello, comptime!\n"))

(defmacro simple-macro ()
  (tokenize-push output (printf "Hello, macros!\\n") (magic-number))
  (return true))

(defmacro magic-number ()
  (tokenize-push output (printf "The magic number is 42"))
  (return true))

(defun-comptime modify-main (environment (& EvaluatorEnvironment)
                                         was-code-modified (& bool)
                                         &return bool)
  (var definition-it (in ObjectDefinitionMap iterator)
       (on-call (field environment definitions) find "main"))
  (when (= definition-it (on-call (field environment definitions) end))
    (printf "modify-main: could not find main!\n")
    (return false))

  (printf "modify-main: found main\n")
  (var definition (& ObjectDefinition) (path definition-it > second))
  (when (!= (FindInContainer (field definition tags) "modify-main-done")
            (on-call (field definition tags) end))
    (printf "modify-main: already modified\n")
    (return true))

  ;; Other modification functions should do this lazily, i.e. only create the expanded definition
  ;; if a modification is necessary
  (var modified-main-tokens (<> std::vector Token))
  (unless (CreateDefinitionCopyMacroExpanded definition modified-main-tokens)
    (return false))

  (prettyPrintTokens modified-main-tokens)

  (var prev-token (* Token) nullptr)
  (for-in token (& Token) modified-main-tokens
       (when (and prev-token
                  (= 0 (on-call (path prev-token > contents) compare "printf"))
                  (ExpectTokenType "modify-main" token TokenType_String))
         (set (field token contents) "I changed your print! Mwahahaha!\\n"))
       (set prev-token (addr token)))
  ;; Next: Relocate old definition, evaluate new definition
  (set was-code-modified true)
  (on-call (field definition tags) push_back "modify-main-done")
  (printf "modify-main: modified main!\n")
  (prettyPrintTokens modified-main-tokens)
  (return true))

(add-compile-time-hook post-references-resolved modify-main)
