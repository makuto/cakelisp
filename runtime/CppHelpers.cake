(skip-build)
(import &comptime-only "CHelpers.cake")

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
  (RequiresFeature
   (field context module)
   (findObjectDefinition environment (call-on c_str (path context . definitionName > contents)))
   RequiredFeature_CppInDefinition name)

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

;; C++-specific features like classes with member functions was never intended to be a first-class
;; feature in Cakelisp. I'm personally more interested in a stricter C-compatible style. However,
;; there are some cases where 3rd-party libraries require you to declare callbacks via classes with
;; member functions, which should be possible from Cakelisp.
(defgenerator defclass (name symbol &rest class-body (index any))
  (var is-local bool false) ;; TODO

  (RequiresFeature
   (field context module)
   (findObjectDefinition environment (call-on c_str (path context . definitionName > contents)))
   RequiredFeature_Cpp name)

  ;; Class declaration
  (var class-declaration-output (& (<> (in std vector) StringOutput))
    (? is-local (field output source) (field output header)))
  (addStringOutput class-declaration-output "class" StringOutMod_SpaceAfter name)
  (addStringOutput class-declaration-output (path name > contents)
                   StringOutMod_ConvertTypeName name)
  (addLangTokenOutput class-declaration-output StringOutMod_OpenBlock name)

  ;; Body of class
  (var end-invocation-index int (FindCloseParenTokenIndex tokens startTokenIndex))
  (var current-index int class-body)
  ;; (var member-variables (<> (in std vector) Token) )
  (while (< current-index end-invocation-index)
    (var current-token (& (const Token)) (at current-index tokens))
    (cond
      ;; Symbols in class bodies always denote a data member declaration
      ((= TokenType_Symbol (field current-token type))
       ;; Get type
       (set current-index (getNextArgument tokens current-index end-invocation-index)))
      ;; Functions etc.
      ((= TokenType_OpenParen (field current-token type))
       (var module-context EvaluatorContext context)
       ;; Classes count as definitions; this clues e.g. defun that it needs ClassName:: prefix
       (set (field module-context definitionName) name)
       (var num-errors int
         (EvaluateGenerate_Recursive environment module-context tokens current-index output))
       (when num-errors (return false))))
    (set current-index (getNextArgument tokens current-index end-invocation-index)))

  ;; End of class
  (addLangTokenOutput class-declaration-output StringOutMod_CloseBlock name)
  (addLangTokenOutput class-declaration-output StringOutMod_EndStatement name)
  (return true))
