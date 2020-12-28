(c-import "<stdio.h>")

(defmacro my-print (message string)
  (printf "Compile-time print!\n")
  (tokenize-push output (printf \"%s\\n\" (token-splice message)))
  (return true))

(defun main(&return int)
  (printf "Hello, world! From Cakelisp!\n")
  (my-print "Printed thanks to a macro!")
  (return 0))
