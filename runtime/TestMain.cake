(import "HotReloading.cake")
(c-import "stdio.h")

(defun main (&return int)
  (printf "Hello Hot-reloading!\n")
  (simple-macro)

  (def-function-signature reload-entry-point-signature (&return bool))
  (var hot-reload-entry-point-func reload-entry-point-signature nullptr)
  (register-function-pointer (type-cast (addr hot-reload-entry-point-func) (* (* void)))
                             ;; TODO Support name conversion at runtime (conversion requires tokens)
                             "reloadableEntryPoint")

  (unless (do-hot-reload)
    (printf "error: failed to hot-reload\n")
    (return 1))

  (while (hot-reload-entry-point-func)
    (unless (do-hot-reload)
      (printf "error: failed to hot-reload\n")
      (return 1)))
  (return 0))

(defun-comptime build-text-adventure ()
  (printf "Hello, comptime!\n"))

(defmacro simple-macro ()
  (tokenize-push output (printf "Hello, macros!\\n"))
  (return true))
