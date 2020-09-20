;; TODO: This should be builtin to macros and generators
(defmacro destructure-arguments ()
  (var end-invocation-index int (FindCloseParenTokenIndex tokens startTokenIndex))
  ;; Find the end invocation for the caller, not us
  (tokenize-push output
                 (var destr-end-invocation-index int
                        (FindCloseParenTokenIndex tokens startTokenIndex)))
  (var start-args-index int (+ 2 startTokenIndex))
  (var current-arg-index int start-args-index)
  ;; Invocation is 0, so skip it
  (var num-destructured-args int 1)
  (while (< current-arg-index end-invocation-index)
    (var current-arg (* (const Token)) (addr (at current-arg-index tokens)))
    (var num-destructured-args-token Token (array TokenType_Symbol (std::to_string num-destructured-args)
                                                  "test/Macros.cake" 1 1 1))
    (unless (ExpectTokenType "destructure-arguments" (at current-arg-index tokens) TokenType_Symbol)
      (return false))
    (var destructured-arg-name-token Token (array TokenType_String (field (at current-arg-index tokens) contents)
                                                  "test/Macros.cake" 1 1 1))
    (tokenize-push output
                   (var (token-splice current-arg) int
                        (getExpectedArgument
                         ;; Use the name of the requested argument as the message
                         (token-splice (addr destructured-arg-name-token))
                         tokens startTokenIndex
                         (token-splice (addr num-destructured-args-token))
                         destr-end-invocation-index))
                   (when (= -1 (token-splice current-arg)) (return false)))
    (++ num-destructured-args)
    (set current-arg-index
         (getNextArgument tokens current-arg-index end-invocation-index)))
  (return true))

;; Assumes tokens is the array of tokens
(defmacro quick-token-at ()
  (destructure-arguments name index)
  (tokenize-push output (var (token-splice (addr (at name tokens))) (& (const Token))
                             (at (token-splice (addr (at index tokens))) tokens)))
  (return true))

;; The first significant generator written in Cakelisp!
(defgenerator def-type-alias ()
  (destructure-arguments name-index type-index)
  (quick-token-at name-token name-index)
  (quick-token-at invocation-token (+ 1 startTokenIndex))

  ;; Make sure the type is valid before outputting anything
  (var type-output (<> std::vector StringOutput))
  (var type-after-name-output (<> std::vector StringOutput))
  (unless (tokenizedCTypeToString_Recursive tokens type-index true type-output type-after-name-output)
    (return false))
  (addModifierToStringOutput (on-call type-output back) StringOutMod_SpaceAfter)

  (addStringOutput (field output source) "typedef" StringOutMod_SpaceAfter (addr invocation-token))
  ;; TODO: Add ability to define typedefs in header
  (PushBackAll (field output source) type-output)

  ;; Evaluate name
  (var expressionContext EvaluatorContext context)
  (set (field expressionContext scope) EvaluatorScope_ExpressionsOnly)
  (unless (= 0 (EvaluateGenerate_Recursive environment expressionContext tokens name-index output))
    (return false))

  ;; Yep, believe it or not, C typedefs have the length of the array after the new type name
  (PushBackAll (field output source) type-after-name-output)
  (addLangTokenOutput (field output source) StringOutMod_EndStatement (addr invocation-token))
  (return true))

;; TODO: Make this take the range stuff in a single argument
(defgenerator for-in ()
  (destructure-arguments name-index type-index range-expression-index start-body-index)
  (quick-token-at name-token name-index)
  (quick-token-at invocation-token (+ 1 startTokenIndex))

  ;; Make sure the type is valid before outputting anything
  (var type-output (<> std::vector StringOutput))
  (var type-after-name-output (<> std::vector StringOutput))
  (unless (tokenizedCTypeToString_Recursive tokens type-index
                                            false ;; No arrays allowed
                                            type-output type-after-name-output)
    (return false))
  (addModifierToStringOutput (on-call type-output back) StringOutMod_SpaceAfter)

  (addStringOutput (field output source) "for" StringOutMod_SpaceAfter (addr invocation-token))
  (addLangTokenOutput (field output source) StringOutMod_OpenParen (addr invocation-token))

  (PushBackAll (field output source) type-output)

  ;; Evaluate counter name
  (var expressionContext EvaluatorContext context)
  (set (field expressionContext scope) EvaluatorScope_ExpressionsOnly)
  (unless (= 0 (EvaluateGenerate_Recursive
                environment expressionContext tokens name-index output))
    (return false))

  (addStringOutput (field output source) ":" StringOutMod_SpaceAfter (addr name-token))

  ;; Evaluate thing we are iterating over
  (unless (= 0 (EvaluateGenerate_Recursive
                environment expressionContext tokens range-expression-index output))
    (return false))
  (addLangTokenOutput (field output source) StringOutMod_CloseParen (addr invocation-token))
  (addLangTokenOutput (field output source) StringOutMod_OpenBlock (addr invocation-token))
  (var bodyContext EvaluatorContext context)
  (set (field bodyContext scope) EvaluatorScope_Body)
  (unless (= 0 (EvaluateGenerateAll_Recursive
                   environment bodyContext tokens start-body-index
                   nullptr output))
    (return false))
  (addLangTokenOutput (field output source) StringOutMod_CloseBlock (addr invocation-token))

  (return true))

(defgenerator function-pointer-cast ()
  ;; TODO: The return type etc. should match cakelisp &return format
  (destructure-arguments variable-index return-type-index arg-types-index)
  (return true))
