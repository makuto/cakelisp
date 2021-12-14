(import "Export.cake")

(c-import "<stdio.h>")

(defun main (&return int)
  (fprintf stderr "Hello, export!\n")
  (test)
  (return 0))
