(import "HotReloading.cake")
(c-import "stdio.h")

(defun main (&return int)
  (unless false
    (printf "Hello Hot-reloading! %d\n" (? true 1 2)))
  (return 0))
