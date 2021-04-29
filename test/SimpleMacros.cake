(c-import "<stdio.h>")

(defmacro my-print (message string)
  (printf "Compile-time print!\n")
  (tokenize-push output (printf "%s\n" (token-splice message)))
  (tokenize-push-new output
                     (printf "%s\n" (token-splice message))
                     (printf "Second one %s\n" (token-splice message)))

  (var numbers (<> (in std vector) Token))
  (tokenize-push-new numbers 1 2 3)
  (tokenize-push-new output (printf "%d %d %d\n" (token-splice-array numbers)))
  (return true))

(defun main(&return int)
  (printf "Hello, world! From Cakelisp!\n")
  (my-print "Printed thanks to a macro!")
  (return 0))
