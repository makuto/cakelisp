(import "HotReloading.cake")
(c-import "stdio.h")

(defun main (&return int)
  (printf "Hello Hot-reloading!\n")
  (var hot-reload-entry-point-func (* void) nullptr)
  (register-function-pointer (addr hot-reload-entry-point-func) "reloadable-entry-point")
  (unless (do-hot-reload)
    (printf "error: failed to hot-reload\n")
    (return 1))

  (while false
      ;;(call (function-pointer-cast bool () hot-reload-entry-point-func))
    (unless (do-hot-reload)
      (printf "error: failed to hot-reload\n")
      (return 1)))
  (return 0))
