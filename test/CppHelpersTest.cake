(add-cakelisp-search-directory "runtime")
(import &comptime-only "CHelpers.cake")

(defclass my-class
    ;; my-type int
  (defun my-member-func (&return bool)
    (return true)))

(defun main (&return int)
  (return 0))
