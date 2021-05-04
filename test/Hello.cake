(add-cakelisp-search-directory "runtime")
(import &comptime-only "CHelpers.cake")
(c-import "<stdio.h>" "<stdarg.h>")

(defun varargs (num-args int &variable-arguments)
  (var list va_list)
  (va_start list num-args)
  (each-in-range num-args i
    (printf "%d\n" (va_arg list int)))
  (va_end list))

(defun main (&return int)
  (printf "Hello, world! From Cakelisp!\n")
  (varargs 3 1 2 3)
  (return 0))
