(c-import "<stdio.h>")
; This is a file to test the tokenizer
(printf "This is a test 0")
;(printf "This is a test 1")
(printf "This is a test 2") ; This is a test comment
; Test weird indentation
(	printf "This is a \"test\" 3"  )
; Test of string delimiters without weird indentation
(printf "This is a \"test\" 3 %s %f" "And another string" -34.4)
(+ 3 2)
(a-crazy-234-symbol+)

(defun test-add (int a int b)
  (+ a b))

(make-+-for-type int)

(+ 3 (to-int 2.2))
