(import "HotReloading.cake")
(c-import "stdio.h")

(def-function-signature-local reload-entry-point-signature (&return bool))

(defun main (&return int)
  (printf "Hello Hot-reloading!\n")
  (var hot-reload-entry-point-func (* void) nullptr)
  (register-function-pointer (addr hot-reload-entry-point-func) "reloadable-entry-point")
  (unless (do-hot-reload)
    (printf "error: failed to hot-reload\n")
    (return 1))

  ;; TODO: Make it possible to cast function pointers properly
  (while false
      (call (type-cast hot-reload-entry-point-func reload-entry-point-signature))
    (unless (do-hot-reload)
      (printf "error: failed to hot-reload\n")
      (return 1)))
  (return 0))
