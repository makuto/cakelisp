(c-import "<stdio.h>")
(c-import "TestHeader.hpp")
(import "TestCakeDependency.cake")

(defun main (&return int)
  (printf "Hello from Cakelisp! My message: %s\nThe answer is %d\n" MY_MESSAGE (the-answer))
  (return 0))
