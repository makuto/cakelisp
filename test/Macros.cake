;; This should actually be a macro, because it doesn't use generator-only features
;; (defgenerator destructure-arguments ()
;;   (var end-invocation-index int (FindCloseParenTokenIndex tokens startTokenIndex))
;;   (var start-args-index int (+ 2 startTokenIndex))
;;   (var current-arg-index int start-args-index)
;;   (var num-destructured-args int 0)
;;   (while (< current-arg-index end-invocation-index)
;;     (var current-token (* (const Token)) (addr (at current-arg-index tokens)))
;;     (when (isSpecialSymbol (deref current-token))
;;       (set current-arg-index (getNextArgument tokens current-arg-index end-invocation-index))
;;       (continue))
;;     (addStringOutput output.source "int" StringOutMod_SpaceAfter current-token)
;;     (addStringOutput output.source (field (at current-arg-index tokens) contents)
;;                      StringOutMod_ConvertVariableName current-token)
;;     (addStringOutput output.source "=" StringOutMod_SpaceBefore current-token)
;;     (addStringOutput output.source (std::to_string num-destructured-args)
;;                      StringOutMod_SpaceBefore current-token)
;;     (addLangTokenOutput output.source StringOutMod_EndStatement current-token)
;;     (set current-arg-index (getNextArgument tokens current-arg-index end-invocation-index))
;;     (set num-destructured-args (+ 1 num-destructured-args)))
;;   (return true))

(defmacro destructure-arguments ()
  (var end-invocation-index int (FindCloseParenTokenIndex tokens startTokenIndex))
  ;; Find the end invocation for the caller, not us
  (tokenize-push output
                 (var destr-end-invocation-index int
                        (FindCloseParenTokenIndex tokens startTokenIndex)))
  (var start-args-index int (+ 2 startTokenIndex))
  (var current-arg-index int start-args-index)
  (var num-destructured-args int 0)
  (while (< current-arg-index end-invocation-index)
    (var current-arg (* (const Token)) (addr (at current-arg-index tokens)))
    (var num-destructured-args-token Token (array TokenType_Symbol (std::to_string num-destructured-args)
                                                  "test/Macros.cake" 1 1 1))
    ;; TODO: This should add expect argument stuff for error checking
    (tokenize-push output
                   (var (token-splice current-arg) int
                        (+ startTokenIndex 2 (token-splice (addr num-destructured-args-token)))))
    (++ num-destructured-args)
    (set current-arg-index
         (getNextArgument tokens current-arg-index end-invocation-index)))
  (printTokens output)
  (return true))

(defmacro def-serialize-struct ()
  (destructure-arguments name-index first-member-index)
  (var end-invocation-index int (FindCloseParenTokenIndex tokens startTokenIndex))
  (var struct-name (& (const Token)) (at name-index tokens))

  (var member-tokens (<> std::vector Token))
  (var serialize-tokens (<> std::vector Token))
  (var current-member int first-member-index)
  (while (< current-member end-invocation-index)
    (var member-name-token (& (const Token)) (at current-member tokens))
    (var member-type-token (& (const Token)) (at (+ 1 current-member) tokens))
    (on-call member-tokens push_back member-name-token)
    (on-call member-tokens push_back member-type-token)
    (when (= 0 (on-call (field member-type-token contents) compare "int"))
      (tokenize-push serialize-tokens
                     (load-int (addr (field struct (token-splice (addr member-type-token)))))))
    (when (= 0 (on-call (field member-type-token contents) compare "bool"))
      (tokenize-push serialize-tokens
                     (load-bool (addr (field struct (token-splice (addr member-type-token)))))))
    ;; Advance once for name and second time for type
    (set current-member (getNextArgument tokens current-member end-invocation-index))
    (set current-member (getNextArgument tokens current-member end-invocation-index)))

  (printTokens member-tokens)

  (tokenize-push output
                 (defstruct (token-splice (addr struct-name))
                   (token-splice-array member-tokens)))

  (var func-name ([] 256 char) (array 0))
  (PrintfBuffer func-name "%s-load" (on-call (field struct-name contents) c_str))
  (var struct-serialize-func-name Token (array TokenType_Symbol func-name
                                               (field struct-name source)
                                               (field struct-name lineNumber)
                                               (field struct-name columnStart)
                                               (field struct-name columnEnd)))
  (when (not (on-call serialize-tokens empty))
      (tokenize-push output
                 (defun (token-splice (addr struct-serialize-func-name)) (out (* (token-splice (addr struct-name)))
                                                                              file (* FILE))
                   ;; TODO: Add token-splice-array
                   (token-splice (addr (at 0 serialize-tokens))))))
  (return true))

(def-serialize-struct serialize-me
    a int
    b bool)
