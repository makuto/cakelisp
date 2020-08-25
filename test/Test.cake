; This is a file to test the tokenizer
(print "This is a test 0")
;(print "This is a test 1")
(print "This is a test 2") ; This is a test comment
; Test weird indentation
(	print "This is a \"test\" 3"  )
; Test of string delimiters without weird indentation
(print "This is a \"test\" 3" "And another string")
(+ 3 2)
(a-crazy-234-symbol+)

(defun test-add (int a int b)
  (+ a b))
