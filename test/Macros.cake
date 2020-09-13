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

;; TODO: The problem here is tokenize-push needs to push its output to the *caller's* output variable
(defmacro internal-tokenize-push ()
  ;; TODO Add expect num arguments
  (var token-str (& (const Token)) (at (+ 3 startTokenIndex) tokens))
  (when (not (ExpectTokenType "tokenize-push" token-str TokenType_String))
     (return false))
  (var tokenize-error (* (const char))
       (tokenizeLine (on-call (field token-str contents) c_str)
                     (field token-str source)
                     (field token-str lineNumber)
                     output))
  (when tokenize-error
    (ErrorAtTokenf (at startTokenIndex tokens) "tokenizer: %s" tokenize-error)
    (return false))
  (return true))

;; lol wat
;; (defmacro tokenize-push ()
;;   (internal-tokenize-push output "(var tokenize-error (* (const char))")
;;   (internal-tokenize-push output "(tokenizeLine (on-call (field token-str contents) c_str)")
;;   (internal-tokenize-push output "(field token-str source)")
;;   (internal-tokenize-push output "(field token-str lineNumber)")
;;   (internal-tokenize-push output "output))")
;;   (internal-tokenize-push output "(when tokenize-error")
;;   (internal-tokenize-push output "(ErrorAtTokenf (at startTokenIndex tokens) \"tokenizer: %s\" tokenize-error)")
;;   (internal-tokenize-push output "(return false))"))

(defmacro def-serialize-struct ()
  ;; (destructure-arguments struct-name &rest body)
  ;; TODO: Make it possible for macros to import definitions of structures defined outside the macro
  (defstruct struct-member-metadata
    name std::string
    is-basic-type bool)

  (defstruct struct-metadata
    name std::string
    members (<> std::vector struct-member-metadata))

  (var metadata struct-metadata (array 0))
  (set metadata.name (field (at struct-name tokens) contents))

  (tokenize-push output "(defstruct " struct-name)

  (var end-invocation-index int (FindCloseParenTokenIndex tokens startTokenIndex))
  (var current-arg int body)
  (while (< current-arg end-invocation-index)
    (metadata.members.push_back (array (field (at current-arg tokens) contents) false))
    (tokenize-push output current-arg))
  (tokenize-push output ")")
  (return true))

(def-serialize-struct serialize-me
    a int)
