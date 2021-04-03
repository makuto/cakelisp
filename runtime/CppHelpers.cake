(skip-build)

(defmacro std-str-equals (std-string-var any str any)
  (tokenize-push output
    (= 0 (call-on compare (token-splice std-string-var) (token-splice str))))
  (return true))
