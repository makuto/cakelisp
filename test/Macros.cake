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
    (tokenize-push output
                   (var (token-splice current-arg) int
                        (+ startTokenIndex 2 (token-splice (addr num-destructured-args-token)))))
    (++ num-destructured-args)
    (set current-arg-index
         (getNextArgument tokens current-arg-index end-invocation-index)))
  (printTokens output)
  (return true))

(defmacro def-serialize-struct ()
  (destructure-arguments test test2)
  (var end-invocation-index int (FindCloseParenTokenIndex tokens startTokenIndex))
  ;; TODO This should use the get expected argument stuff for safety
  (var struct-name (* (const Token)) (addr (at (+ 2 startTokenIndex) tokens)))
  (var arg-tokens (<> std::vector Token))
  (var current-arg int (+ 3 startTokenIndex))
  (while (< current-arg end-invocation-index)
    (on-call arg-tokens push_back (at current-arg tokens))
    (on-call arg-tokens push_back (at (+ 1 current-arg) tokens))
    ;; Advance once for name and second time for type
    (set current-arg (getNextArgument tokens current-arg end-invocation-index))
    (set current-arg (getNextArgument tokens current-arg end-invocation-index)))

  (printTokens arg-tokens)

  (tokenize-push output
                 (defstruct (token-splice struct-name)
                   (token-splice-array arg-tokens)))
  (return true))

(def-serialize-struct serialize-me
    a int
    b bool)
