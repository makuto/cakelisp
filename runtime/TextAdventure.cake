(c-import "<stdio.h>")

;; Return true to hot-reload, or false to exit
(defun reloadable-entry-point (&return bool)
  (printf "Cake Adventure\n")
  (return false))
