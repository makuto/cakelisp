(c-import "<stdio.h>")
(c-import "TestHeader.hpp")

(defun main (&return int)
  (printf "Hello from Cakelisp! My message: %s\n" MY_MESSAGE)
  (return 0))
