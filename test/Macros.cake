(defgenerator destructure-arguments ()
  (var end-invocation-index int (FindCloseParenTokenIndex tokens startTokenIndex))
  (var start-args-index int (+ 1 startTokenIndex))
  (var current-arg-index int start-args-index)
  (var num-destructured-args int 0)
  (while (< current-arg-index end-invocation-index)
    (var current-token (* (const Token)) (addr (at current-arg-index tokens)))
    (addStringOutput output.source "int" StringOutMod_SpaceAfter current-token)
    (addStringOutput output.source (field (at current-arg-index tokens) contents)
                     StringOutMod_ConvertVariableName current-token)
    (addStringOutput output.source "=" StringOutMod_SpaceBefore current-token)
    (addStringOutput output.source (std::to_string current-arg-index) StringOutMod_SpaceBefore current-token)
    (addLangTokenOutput output.source StringOutMod_EndStatement current-token)
    (set current-arg-index (getNextArgument tokens current-arg-index end-invocation-index))
    (set num-destructured-args (+ 1 num-destructured-args)))
  (return true))

(defmacro def-serialize-struct ()
  (destructure-arguments struct-name &rest body)
  ;; TODO: Make it possible for macros to import definitions of structures defined outside the macro
  (defstruct struct-member-metadata
    name std::string
    is-basic-type bool)

  (defstruct struct-metadata
    name std::string
    members (<> std::vector struct-member-metadata))

  (var metadata struct-metadata (array 0))
  (set metadata.name (field (at struct-name tokens) contents))

  (tokenize-push output "(defstruct" struct-name)

  (var end-invocation-index int (FindCloseParenTokenIndex tokens startTokenIndex))
  (var current-arg int (get-token-arg 0 body))
  (while (< current-arg end-invocation-index)
    (metadata.members.push_back (array (at current-arg tokens)))
    (tokenize-push output current-arg))
  (tokenize-push ")")
  (return true))

(def-serialize-struct serialize-me
    a int)
