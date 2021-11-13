(import &comptime-only "CppHelpers.cake")

;; Binds the variable's address to the named var
;; Note that this causes the caller's function to return false if the binding failed
;; TODO: This is madness, or close to it. All this for every comptime variable reference...
(defmacro get-or-create-comptime-var (bound-var-name (ref symbol) var-type (ref any)
                                      &optional initializer-index (index any))
  (unless (field environment moduleManager)
    (return false))

  (unless (ExpectTokenType "get-or-create-comptime-var" bound-var-name TokenType_Symbol)
    (return false))

  ;; Only basing off of var-type for blaming
  (var var-type-str Token var-type)
  (set (field var-type-str type) TokenType_String)
  (var destroy-var-func-name-str Token var-type)
  (set (field destroy-var-func-name-str type) TokenType_String)
  ;; Convert type to parseable string as well as unique (to the type) function name
  (scope
   ;; For parsing
   (var type-to-string-buffer ([] 256 char) (array 0))
   (var type-string-write-head (* char) type-to-string-buffer)
   ;; For function name
   (var type-to-name-string-buffer ([] 256 char) (array 0))
   (var type-name-string-write-head (* char) type-to-name-string-buffer)
   (unless (writeStringToBufferErrorToken "destroy-"
                                          (addr type-name-string-write-head) type-to-name-string-buffer
                                          (sizeof type-to-name-string-buffer) var-type)
     (return false))

   (var current-type-token (* (const Token)) (addr var-type))
   (var end-type-token (* (const Token)) (FindTokenExpressionEnd current-type-token))
   (while (<= current-type-token end-type-token)
     (unless (appendTokenToString (deref current-type-token) (addr type-string-write-head)
                                  type-to-string-buffer (sizeof type-to-string-buffer))
       (return false))

     (when (= (path current-type-token > type) TokenType_Symbol)
       (cond
         ((std-str-equals (path current-type-token > contents) "*")
          (unless (writeStringToBufferErrorToken "ptr-to-"
                                                 (addr type-name-string-write-head) type-to-name-string-buffer
                                                 (sizeof type-to-name-string-buffer) (deref current-type-token))
            (return false)))
         ((std-str-equals (path current-type-token > contents) "<>")
          (unless (writeStringToBufferErrorToken "tmpl-of-"
                                                 (addr type-name-string-write-head) type-to-name-string-buffer
                                                 (sizeof type-to-name-string-buffer) (deref current-type-token))
            (return false)))
         ((std-str-equals (path current-type-token > contents) "&")
          (unless (writeStringToBufferErrorToken "ref-to-"
                                                 (addr type-name-string-write-head) type-to-name-string-buffer
                                                 (sizeof type-to-name-string-buffer) (deref current-type-token))
            (return false)))
         ((std-str-equals (path current-type-token > contents) "[]")
          (unless (writeStringToBufferErrorToken "array-of-"
                                                 (addr type-name-string-write-head) type-to-name-string-buffer
                                                 (sizeof type-to-name-string-buffer) (deref current-type-token))
            (return false)))
         ((std-str-equals (path current-type-token > contents) "const")
          (unless (writeStringToBufferErrorToken "const-"
                                                 (addr type-name-string-write-head) type-to-name-string-buffer
                                                 (sizeof type-to-name-string-buffer) (deref current-type-token))
            (return false)))
         ;; Type names
         (true
          (unless (writeStringToBufferErrorToken (call-on c_str (path current-type-token > contents))
                                                 (addr type-name-string-write-head) type-to-name-string-buffer
                                                 (sizeof type-to-name-string-buffer) (deref current-type-token))
            (return false)))))
     (incr current-type-token))
   (set (field var-type-str contents) type-to-string-buffer)

   (var current-char (* char) type-to-name-string-buffer)
   (while (!= (deref current-char) 0)
     (when (= (deref current-char) ':')
       (set (deref current-char) '-'))
     (incr current-char))
   (set (field destroy-var-func-name-str contents) type-to-name-string-buffer))

  (var var-name Token bound-var-name)
  (set (field var-name type) TokenType_String)

  (var destroy-var-func-name-symbol Token destroy-var-func-name-str)
  (set (field destroy-var-func-name-symbol type) TokenType_Symbol)

  (var destroy-func-name (* (const char))
    (call-on c_str (field destroy-var-func-name-str contents)))

  ;; Define the destructor if one for this type isn't already defined
  (unless (or (findCompileTimeFunction environment destroy-func-name)
              (findObjectDefinition environment destroy-func-name))
    (var destruction-func-def (* (<> std::vector Token)) (new (<> std::vector Token)))
    ;; Need to have the environment delete this once it's safe
    (call-on push_back (field environment comptimeTokens) destruction-func-def)
    (tokenize-push (deref destruction-func-def)
      (defun-comptime (token-splice-addr destroy-var-func-name-symbol) (data (* void))
        (delete (type-cast data (* (token-splice-addr var-type))))))

    (var destruction-func-context EvaluatorContext context)
    ;; This doesn't cause the required to propagate because comptime functions are lazily required,
    ;; even in module scope, because they're super slow to build (don't build if you don't use)
    (set (field destruction-func-context isRequired) true)
    (set (field destruction-func-context scope) EvaluatorScope_Module)
    (set (field destruction-func-context definitionName)
         (addr (path environment . moduleManager > globalPseudoInvocationName)))
    ;; We are only outputting a compile-time function, which uses definition's output storage to be
    ;; built. This throwaway will essentially only have a splice to that output, so we don't really
    ;; need to keep track of it, except to destroy it once everything is done
    (var throwaway-output (* GeneratorOutput) (new GeneratorOutput))
    (call-on push_back (field environment orphanedOutputs) throwaway-output)
    (unless (= 0 (EvaluateGenerate_Recursive environment
                                             destruction-func-context
                                             (deref destruction-func-def) 0
                                             (deref throwaway-output)))
      (return false)))

  (var initializer (<> std::vector Token))
  (when (!= initializer-index -1)
    (tokenize-push initializer (set (deref (token-splice-addr bound-var-name))
                                    (token-splice-addr (at initializer-index tokens)))))

  ;; Create the binding and lazy-variable creation
  ;; TODO: Any way to make this less code for each ref? There's a lot here.
  ;; Yes: Auto-generate construction function and call it instead of copy-pasting
  (tokenize-push output
    (var (token-splice-addr bound-var-name) (* (token-splice-addr var-type)) null)
    (scope
     (unless (GetCompileTimeVariable environment
                                     (token-splice-addr var-name) (token-splice-addr var-type-str)
                                     (type-cast (addr (token-splice-addr bound-var-name)) (* (* void))))
       (set (token-splice-addr bound-var-name) (new (token-splice-addr var-type)))
       (token-splice-array initializer)
       (var destroy-func-name (* (const char)) (token-splice-addr destroy-var-func-name-str))
       (unless (CreateCompileTimeVariable environment
                                          (token-splice-addr var-name) (token-splice-addr var-type-str)
                                          (type-cast (token-splice-addr bound-var-name) (* void))
                                          destroy-func-name)
         (delete (token-splice-addr bound-var-name))
         (return false)))))
  (return true))

;; TODO: This is now built in, but it would still be useful to bind to arbitrary tokens
;; (defmacro destructure-arguments ()
;;   (var end-invocation-index int (FindCloseParenTokenIndex tokens startTokenIndex))
;;   ;; Find the end invocation for the caller, not us
;;   (tokenize-push output
;;                  (var destr-end-invocation-index int
;;                       (FindCloseParenTokenIndex tokens startTokenIndex)))
;;   (var start-args-index int (+ 2 startTokenIndex))
;;   (var current-arg-index int start-args-index)
;;   ;; Invocation is 0, so skip it
;;   (var num-destructured-args int 1)
;;   (while (< current-arg-index end-invocation-index)
;;     (var current-arg (* (const Token)) (addr (at current-arg-index tokens)))
;;     (var num-destructured-args-token Token (array TokenType_Symbol (std::to_string num-destructured-args)
;;                                                   "test/ComptimeHelpers.cake" 1 1 1))
;;     (unless (ExpectTokenType "destructure-arguments" (at current-arg-index tokens) TokenType_Symbol)
;;       (return false))
;;     (var destructured-arg-name-token Token (array TokenType_String (field (at current-arg-index tokens) contents)
;;                                                   "test/ComptimeHelpers.cake" 1 1 1))
;;     (tokenize-push output
;;                    (var (token-splice current-arg) int
;;                         (getExpectedArgument
;;                          ;; Use the name of the requested argument as the message
;;                          (token-splice (addr destructured-arg-name-token))
;;                          tokens startTokenIndex
;;                          (token-splice (addr num-destructured-args-token))
;;                          destr-end-invocation-index))
;;                    (when (= -1 (token-splice current-arg)) (return false)))
;;     (++ num-destructured-args)
;;     (set current-arg-index
;;          (getNextArgument tokens current-arg-index end-invocation-index)))
;;   (return true))

;; Assumes tokens is the array of tokens
(defmacro quick-token-at (name symbol index any)
  (tokenize-push output (var (token-splice name) (& (const Token))
                          (at (token-splice index) tokens)))
  (return true))

(defmacro command-add-string-argument (command any new-argument any)
  (tokenize-push output
    (call-on push_back (field (token-splice command) arguments)
             (array ProcessCommandArgumentType_String
                    (token-splice new-argument))))
  (return true))

(defmacro c-statement-out (statement-operation symbol)
  (tokenize-push output
    (CStatementOutput environment context tokens startTokenIndex
                      (token-splice statement-operation)
                      (array-size (token-splice statement-operation))
                      output))
  (return true))

;; Create a symbol which is unique in the current context (e.g. function body)
(defmacro gen-unique-symbol (token-var-name symbol prefix string reference-token any)
  (tokenize-push
   output
   (var (token-splice token-var-name) Token (token-splice reference-token))
   (MakeContextUniqueSymbolName environment context (token-splice prefix)
                                (addr (token-splice token-var-name))))
  (return true))

;; (defmacro each-token-argument-in (token-array any
;;                                   start-index any
;;                                   iterator-name symbol
;;                                   &rest body any)
;;   (gen-unique-symbol end-token-var "end-token" (deref start-index))
;;   (tokenize-push output
;;     (scope
;;      (var (token-splice-addr end-token-var) int
;;        (FindCloseParenTokenIndex (token-splice token-array start-index)))
;;      (c-for (var (token-splice iterator-name) int (token-splice start-index))
;;          (< (token-splice iterator-name) (token-splice-addr end-token-var))
;;          (set (token-splice iterator-name)
;;               (getNextArgument (token-splice token-array iterator-name (addr end-token-var))))
;;        (token-splice-rest body tokens))))
;;   (return true))

(defmacro each-token-argument-in (token-array any
                                  start-index any
                                  end-token-index any
                                  iterator-name symbol
                                  &rest body any)
  (tokenize-push output
    (scope
     (c-for (var (token-splice iterator-name) int (token-splice start-index))
         (< (token-splice iterator-name) (token-splice end-token-index))
         (set (token-splice iterator-name)
              (getNextArgument (token-splice token-array iterator-name end-token-index)))
       (token-splice-rest body tokens))))
  (return true))

(defmacro token-contents-snprintf (token any format string &rest arguments any)
  (tokenize-push output
    (scope
     (var token-contents-printf-buffer ([] 256 char) (array 0))
     (var num-printed size_t
       (snprintf token-contents-printf-buffer (sizeof token-contents-printf-buffer)
                 (token-splice format)
                 (token-splice-rest arguments tokens)))
     (when (>= num-printed (sizeof token-contents-printf-buffer))
       (fprintf stderr "error: token-contents-snprintf printed more characters than can fit in " \
                "buffer of size %d (%d)\n"
                (type-cast (sizeof token-contents-printf-buffer) int)
                (type-cast num-printed int)))
     (set (field (token-splice token) contents) token-contents-printf-buffer)))
  (return true))
