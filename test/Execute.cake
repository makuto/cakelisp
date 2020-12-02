(add-cakelisp-search-directory "runtime")
(add-c-search-directory global "runtime") ;; Should this happen automatically?
(import "HotReloading.cake")

(c-import "<stdio.h>")

(defun main (&return int)
  (printf "Hello, execute!\n")
  (return 0))

(set-cakelisp-option executable-output "test/ExecuteMe")
