(include "Macros.cake")

(include "<unordered_map>")
(include "<vector>")

;; The first significant generator written in Cakelisp!
(defgenerator def-type-alias ()
  (destructure-arguments name-index type-index)
  (quick-token-at name-token name-index)
  (quick-token-at invocation-token (+ 1 startTokenIndex))

  ;; Make sure the type is valid before outputting anything
  (var type-output (<> std::vector StringOutput))
  (var type-after-name-output (<> std::vector StringOutput))
  (when (not (tokenizedCTypeToString_Recursive tokens type-index true type-output type-after-name-output))
    (return false))
  (addModifierToStringOutput (on-call type-output back) StringOutMod_SpaceAfter)

  (addStringOutput (field output source) "typedef" StringOutMod_SpaceAfter (addr invocation-token))
  ;; TODO: Add ability to define typedefs in header
  (PushBackAll (field output source) type-output)

  ;; Evaluate name
  (var expressionContext EvaluatorContext context)
  (set (field expressionContext scope) EvaluatorScope_ExpressionsOnly)
  (when (not (= 0 (EvaluateGenerate_Recursive environment expressionContext tokens name-index output)))
    (return false))

  ;; Yep, believe it or not, C typedefs have the length of the array after the new type name
  (PushBackAll (field output source) type-after-name-output)
  (addLangTokenOutput (field output source) StringOutMod_EndStatement (addr invocation-token))
  (return true))

(def-type-alias FunctionReferenceMap (<> std::unordered_map std::string (<> std::vector (* (* void)))))
(def-type-alias FunctionReferenceMapIterator (in FunctionReferenceMap iterator))

(var registered-functions FunctionReferenceMap)

(defun register-function-pointer (function-pointer (* (* void))
                                  function-name (* (const char)))
  (var findIt FunctionReferenceMapIterator
       (on-call registered-functions find function-name))
  ;; TODO: Add if statement
  (if (= findIt (on-call registered-functions end))
      (set (at function-name registered-functions) function-pointer)
      (on-call (field findIt second) push_back function-pointer)))
