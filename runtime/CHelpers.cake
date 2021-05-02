(skip-build)

(import &comptime-only "ComptimeHelpers.cake")

;; This should evaluate its argument, but I'm just hacking it in right now anyways
(defmacro array-size (array-token symbol)
  (tokenize-push output (/ (sizeof (token-splice array-token))
                           (sizeof (at 0 (token-splice array-token)))))
  (return true))

;; Necessary to create e.g. in C PREFIX "_my_thing"
(defgenerator static-string-combine (string-A (arg-index any) string-B (arg-index any))
  (var statement (const ([] CStatementOperation))
    (array
     (array Expression null string-A)
     (array Keyword " " -1)
     (array Expression null string-B)))
  (return (c-statement-out statement)))

;; cakelisp's tokenizer doesn't properly parse ' '
(defgenerator space-hack ()
  (var statement (const ([] CStatementOperation))
    (array
     (array Keyword "' '" -1)))
  (return (c-statement-out statement)))

;;
;; Declarations
;;

;; Given
;; (declare-extern-function my-func (i int &return bool))
;; Output
;; bool myFunc(int i);
;; This is useful for forward declarations of functions or declaring functions linked dynamically
(defgenerator declare-extern-function (name-token (ref symbol) signature-index (index array))
  (quick-token-at signature-token signature-index)
  (var return-type-start int -1)
  (var arguments (<> std::vector FunctionArgumentTokens))
  (unless (parseFunctionSignature tokens signature-index arguments return-type-start)
	(return false))

  (var end-signature-index int (FindCloseParenTokenIndex tokens signature-index))
  (unless (outputFunctionReturnType environment context tokens output return-type-start
                                    startTokenIndex
	                                end-signature-index
	                                true ;; Output to source
                                    false) ;; Output to header
    (return false))

  (addStringOutput (path output . source) (field name-token contents)
                   StringOutMod_ConvertFunctionName
	               (addr name-token))

  (addLangTokenOutput (field output source) StringOutMod_OpenParen (addr signature-token))

  (unless (outputFunctionArguments environment context tokens output arguments
                                   true ;; Output to source
                                   false) ;; Output to header
	(return false))

  (addLangTokenOutput (field output source) StringOutMod_CloseParen (addr signature-token))

  (addLangTokenOutput (field output source) StringOutMod_EndStatement (addr signature-token))
  (return true))

;; Creates struct/class forward declarations in header files.
;; Example usage:
;; (forward-declare (namespace Ogre (class item) (struct my-struct)))
;; Outputs namespace Ogre { class item; struct my-struct;}
(defgenerator forward-declare (&rest start-body-index (index any))
  ;; TODO: Support global vs local?
  (var is-global bool true)
  (var output-dest (& (<> std::vector StringOutput))
    (? is-global (field output header) (field output source)))

  (var end-invocation-index int (FindCloseParenTokenIndex tokens startTokenIndex))
  (var current-index int start-body-index)
  (var namespace-stack (<> std::vector int))
  (while (< current-index end-invocation-index)
    (var current-token (& (const Token)) (at current-index tokens))
    ;; Invocations
    (when (= TokenType_OpenParen (field current-token type))
      (var invocation-token (& (const Token)) (at (+ 1 current-index) tokens))
      (cond
        ((= 0 (call-on compare (field invocation-token contents) "namespace"))
         (unless (< (+ 3 current-index) end-invocation-index)
           (ErrorAtToken invocation-token "missing name or body arguments")
           (return false))
         (var namespace-name-token (& (const Token)) (at (+ 2 current-index) tokens))
         (addStringOutput output-dest "namespace"
                          StringOutMod_SpaceAfter (addr invocation-token))
         (addStringOutput output-dest (field namespace-name-token contents)
                          StringOutMod_None (addr namespace-name-token))
         (addLangTokenOutput output-dest StringOutMod_OpenBlock (addr namespace-name-token))
         (call-on push_back namespace-stack (FindCloseParenTokenIndex tokens current-index)))

        ((or (= 0 (call-on compare (field invocation-token contents) "class"))
             (= 0 (call-on compare (field invocation-token contents) "struct")))
         (unless (< (+ 2 current-index) end-invocation-index)
           (ErrorAtToken invocation-token "missing name argument")
           (return false))
         (var type-name-token (& (const Token)) (at (+ 2 current-index) tokens))
         (unless (ExpectTokenType "forward-declare" type-name-token TokenType_Symbol)
           (return false))
         (addStringOutput output-dest (field invocation-token contents)
                          StringOutMod_SpaceAfter (addr invocation-token))
         (addStringOutput output-dest (field type-name-token contents)
                          StringOutMod_None (addr type-name-token))
         (addLangTokenOutput output-dest StringOutMod_EndStatement (addr type-name-token)))
        (true
         (ErrorAtToken invocation-token "unknown forward-declare type")
         (return false))))

    (when (= TokenType_CloseParen (field current-token type))
      (for-in close-block-index int namespace-stack
              (when (= close-block-index current-index)
                (addLangTokenOutput output-dest StringOutMod_CloseBlock
                                    (addr (at current-index tokens))))))
    ;; TODO: Support function calls so we can do this recursively?
    ;; (set current-index
    ;; (getNextArgument tokens current-index end-invocation-index))
    (incr current-index))
  (return true))


;;
;; Iteration/Looping
;;

(defgenerator c-for (initializer (arg-index any)
                     conditional (arg-index any)
                     update (arg-index any)
                     ;; Cannot be optional due to CStatementOutput limitation
                     &rest body (arg-index any))
  (var statement (const ([] CStatementOperation))
    (array
     (array Keyword "for" -1)
     (array OpenParen null -1)
     (array Statement null initializer)
     (array Expression null conditional)
     (array Keyword ";" -1)
     (array Expression null update)
     (array CloseParen null -1)
     (array OpenBlock null -1)
     (array Body null body)
     (array CloseBlock null -1)))
  (return (c-statement-out statement)))

;; This only works for arrays where the size is known at compile-time
(defmacro each-in-array (array-name symbol iterator-name symbol &rest body any)
  (tokenize-push output
    (scope
     (each-in-range (array-size (token-splice array-name)) (token-splice iterator-name)
       (token-splice-rest body tokens))))
  (return true))

;; Note: Will reevaluate the range expression each iteration
(defmacro each-in-range (range any iterator-name symbol &rest body any)
  (tokenize-push output
    (c-for (var (token-splice iterator-name) int 0)
        (< (token-splice iterator-name) (token-splice range))
        (incr (token-splice iterator-name))
      (token-splice-rest body tokens)))
  (return true))

(defmacro each-char-in-string (start-char any iterator-name symbol &rest body any)
  (tokenize-push output
    (c-for (var (token-splice iterator-name) (* char) (token-splice start-char))
        (deref (token-splice iterator-name))
        (incr (token-splice iterator-name))
      (token-splice-rest body tokens)))
  (return true))

(defmacro each-char-in-string-const (start-char any iterator-name symbol &rest body any)
  (tokenize-push output
    (c-for (var (token-splice iterator-name) (* (const char)) (token-splice start-char))
        (deref (token-splice iterator-name))
        (incr (token-splice iterator-name))
      (token-splice-rest body tokens)))
  (return true))

;;
;; Preprocessor
;;

(defgenerator define-constant (define-name (arg-index symbol) value (arg-index any))
  (var define-statement (const ([] CStatementOperation))
    (array
     (array Keyword "#define" -1)
     (array Expression null define-name)
     (array Keyword " " -1)
     (array Expression null value)
     (array KeywordNoSpace "\n" -1)))
  (return (c-statement-out define-statement)))

(defgenerator undefine-constant (define-name symbol)
  (var define-statement (const ([] CStatementOperation))
    (array
     (array Keyword "#undef" -1)
     (array Expression null 1)
     (array KeywordNoSpace "\n" -1)))
  (return (c-statement-out define-statement)))

(defgenerator if-c-preprocessor-defined (preprocessor-symbol (arg-index symbol)
                                         true-block (arg-index any)
                                         &optional false-block (arg-index any))
  (if (!= -1 false-block)
      (scope
       (var statement (const ([] CStatementOperation))
         (array
          (array Keyword "#ifdef" -1)
          (array Expression null preprocessor-symbol)
          (array KeywordNoSpace "\n" -1)
          (array Statement null true-block)
          (array KeywordNoSpace "#else" -1)
          (array KeywordNoSpace "\n" -1)
          (array Statement null false-block)
          (array KeywordNoSpace "#endif" -1)
          (array KeywordNoSpace "\n" -1)))
       (return (c-statement-out statement)))
      (scope
       (var statement (const ([] CStatementOperation))
         (array
          (array Keyword "#ifdef" -1)
          (array Expression null preprocessor-symbol)
          (array KeywordNoSpace "\n" -1)
          (array Statement null true-block)
          (array KeywordNoSpace "#endif" -1)
          (array KeywordNoSpace "\n" -1)))
       (return (c-statement-out statement)))))
