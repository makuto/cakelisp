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

;; The problem here is tokenize-push needs to push its output to the *caller's* output variable
;; (defmacro internal-tokenize-push ()
;;   ;; TODO Add expect num arguments
;;   (var token-str (& (const Token)) (at (+ 3 startTokenIndex) tokens))
;;   (when (not (ExpectTokenType "tokenize-push" token-str TokenType_String))
;;      (return false))
;;   (var tokenize-error (* (const char))
;;        (tokenizeLine (on-call (field token-str contents) c_str)
;;                      (field token-str source)
;;                      (field token-str lineNumber)
;;                      output))
;;   (when tokenize-error
;;     (ErrorAtTokenf (at startTokenIndex tokens) "tokenizer: %s" tokenize-error)
;;     (return false))
;;   (return true))

;; lol wat
;; (defmacro tokenize-push ()
;;   (tokenizeLine "(var tokenize-error (* (const char))" "Macros.cake" 41 output)
;;   (tokenizeLine "(tokenizeLine (on-call (field token-str contents) c_str)" "Macros.cake" 41 output)
;;   (tokenizeLine "(field token-str source)" "Macros.cake" 41 output)
;;   (tokenizeLine "(field token-str lineNumber)" "Macros.cake" 41 output)
;;   (tokenizeLine "output))" "Macros.cake" 41 output)
;;   (tokenizeLine "(when tokenize-error" "Macros.cake" 41 output)
;;   (tokenizeLine "(ErrorAtTokenf (at startTokenIndex tokens) \"tokenizer: %s\" tokenize-error)" "Macros.cake" 41 output)
;;   (tokenizeLine "(return false))" "Macros.cake" 41 output)
;;   (return true))

;; (defmacro def-serialize-struct ()
;;   ;; (destructure-arguments struct-name &rest body)
;;   ;; TODO: Make it possible for macros to import definitions of structures defined outside the macro
;;   (defstruct struct-member-metadata
;;     name std::string
;;     is-basic-type bool)

;;   (defstruct struct-metadata
;;     name std::string
;;     members (<> std::vector struct-member-metadata))

;;   (var metadata struct-metadata (array 0))
;;   (set metadata.name (field (at struct-name tokens) contents))

;;   (tokenize-push output (defstruct (token-splice struct-name) (token-splice struct-members)))

;;   (tokenize-push output "(defstruct " struct-name)
;;   ;; Should output
;;   ;; (tokenizeLine "(defstruct" output)
;;   ;; (on-call output push_back (array TokenType_Symbol (field struct-name contents)
;;   ;;                            source line-number column-start column-end))

;;   (var end-invocation-index int (FindCloseParenTokenIndex tokens startTokenIndex))
;;   (var current-arg int body)
;;   (while (< current-arg end-invocation-index)
;;     (metadata.members.push_back (array (field (at current-arg tokens) contents) false))
;;     (tokenize-push output current-arg))
;;   (tokenize-push output ")")
;;   (return true))

(defmacro def-serialize-struct ()
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
                 (defstruct (token-splice-expr struct-name)
                   (token-splice-array arg-tokens)))
  (return true))

(def-serialize-struct serialize-me
    a int
    b bool)
