(skip-build)

(defmacro std-str-equals (std-string-var any str any)
  (tokenize-push output
    (= 0 (call-on compare (token-splice std-string-var) (token-splice str))))
  (return true))

;; Given
;; (var-construct name type constructor-arguments)
;; Output
;; type name(constructor-arguments);
(defgenerator var-construct (name (arg-index symbol) type (arg-index any)
                             &rest constructor-arguments (arg-index any))
  (var constructor-var-statement (const ([] CStatementOperation))
    (array
     (array TypeNoArray null type)
     (array Keyword " " -1)
     (array Expression null name)
     (array OpenParen null -1)
     (array ExpressionList null constructor-arguments)
     (array CloseParen null -1)
     (array SmartEndStatement null -1)))
  (return (CStatementOutput environment context tokens startTokenIndex
                            constructor-var-statement (array-size constructor-var-statement)
                            output)))
