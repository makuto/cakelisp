(add-cakelisp-search-directory "runtime")
(import &comptime-only "CHelpers.cake")

(defenum my-enum
  my-enum-a
  my-enum-b
  my-enum-c)

(defenum-local my-enum-local
  my-enum-local-a
  my-enum-local-b
  my-enum-local-c)

(defclass my-class
    ;; my-type int
  (defun my-member-func (&return bool)
    (return true)))

(defun main (&return int)
  (var state my-enum my-enum-a)
  (return 0))
