(c-import "<stdio.h>")

;; (comptime-cond
;;  ('Unix
;;   (comptime-error "This is a Unix machine"))
;;  ('Windows
;;   (comptime-error "This is a Windows machine"))
;;  (true
;;   (comptime-error
;;    "This module requires platform-specific code. Please define your platform before importing this module, e.g. (comptime-define-symbol 'Unix)")))

(c-preprocessor-define MAX_PATH_LENGTH 255)


(defun main (&return int)
  (printf "Path length: %d\n" MAX_PATH_LENGTH)

  (def-type-alias my-type int)
  (var a-thing my-type 0)
  (printf "Thing: %d\n" a-thing)
  (return 0))
