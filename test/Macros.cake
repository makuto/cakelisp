(defgenerator destructure-arguments ()
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
  (set metadata.name (at struct-name tokens))

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
