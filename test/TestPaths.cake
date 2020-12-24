(c-import "<stdio.h>"
        ;; realpath
        "<stdlib.h>")
(add-cakelisp-search-directory "runtime")
(import &comptime-only "Macros.cake")

(defun main (&return int)
  (var path-tests ([] (* (const char))) (array
                                         "."
                                         "./runtime/TestPaths.cake"
                                         "/home/macoy/Development/code/repositories/cakelisp"
                                         "../cakelisp/runtime/TestPaths.cake"
                                         "runtime/Macros.cake"
                                         "src/Macros.cake"))
  (var i int 0)
  (while (< i (array-size path-tests))
    (var result (* (const char)) (realpath (at i path-tests) null))
    (unless result
      (perror "realpath: ")
      (incr i)
      (continue))
    (printf "%s = %s\n" (at i path-tests) result)
    (free (type-cast result (* void)))
    (incr i))
  (return 0))
