(import "HotReloading.cake")
(c-import "stdio.h")

(defun main (&return int)
  (fprintf stderr "Hello Hot-reloading!\n")

  (def-function-signature reload-entry-point-signature (&return bool))
  (var hot-reload-entry-point-func reload-entry-point-signature null)
  (register-function-pointer (type-cast (addr hot-reload-entry-point-func) (* (* void)))
                             ;; TODO Support name conversion at runtime? (conversion requires tokens)
                             "reloadableEntryPoint")

  (unless (do-hot-reload)
    (fprintf stderr "error: failed to hot-reload\n")
    (hot-reload-clean-up)
    (return 1))

  (while (hot-reload-entry-point-func)
    (unless (do-hot-reload)
      (fprintf stderr "error: failed to hot-reload\n")
      (hot-reload-clean-up)
      (return 1)))

  (hot-reload-clean-up)
  (return 0))

(comptime-cond
 ('Unix
  (set-cakelisp-option executable-output "hot_loader"))
 ('Windows
  (set-cakelisp-option executable-output "hot_loader.exe")))
