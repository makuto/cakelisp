(c-import "<stdio.h>")

(defun main (&return int)
  (printf "Hello, execute! 4\n")
  (return 0))

(set-cakelisp-option executable-output "test/ExecuteMe")
(add-build-config-label "Debug" "2")
