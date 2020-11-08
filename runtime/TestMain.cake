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

(defun-comptime modify-main (environment (& EvaluatorEnvironment)
                                         was-code-modified (& bool)
                                         &return bool)
  (for-in name-definition (& ObjectDefinitionPair) (field environment definitions)
          (unless (= 0 (on-call (field name-definition first) compare "main"))
            (continue))
          (printf "found main\n"))
  (return true))

(add-compile-time-hook post-references-resolved modify-main)
