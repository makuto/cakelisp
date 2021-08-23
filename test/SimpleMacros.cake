(c-import "<stdio.h>")

(defmacro argument-indices (first (arg-index string) &optional second (arg-index any))
  (Logf "first arg has index %d, second has index %d\n" first second)
  (return true))

(defmacro my-print (message string)
  (printf "Compile-time print!\n")
  (tokenize-push output (printf "%s\n" (token-splice message)))
  (tokenize-push output
    (printf "%s\n" (token-splice message))
    (printf "Second one %s\n" (token-splice message)))

  (var numbers (<> (in std vector) Token))
  (tokenize-push numbers 1 2 3)
  (tokenize-push output (printf "%d %d %d\n" (token-splice-array numbers)))
  (return true))

(defun main (&return int)
  (printf "Hello, world! From Cakelisp!\n")
  (my-print "Printed thanks to a macro!")
  (argument-indices "test")
  (argument-indices "test" "test 2")
  (return 0))
