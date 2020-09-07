(include "<stdio.h>")

(defun main (&return int)
  (macro-a)
  ;; (macro-b)
  (return 0))

(defmacro macro-a ()
  (return true))
