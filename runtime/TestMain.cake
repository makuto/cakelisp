(import "HotReloading.cake")
(c-import "stdio.h")

(defun main (&return int)
  (printf "Hello Hot-reloading!\n")
  (return 0))
