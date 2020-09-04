(include "<stdio.h>")
(include "TestHeader.hpp")
(include "TestCakeDependency.cake")

(defun main (&return int)
  (printf "Hello from Cakelisp! My message: %s\nThe answer is %d\n" MY_MESSAGE (the-answer))
  (return 0))
