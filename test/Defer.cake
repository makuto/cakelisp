(add-cakelisp-search-directory "runtime")
(import "CHelpers.cake")

(c-import "<stdio.h>" "<stdlib.h>")

(defun test-defer ()
  (var-cast-to buffer (* char) (malloc 1024))
  (defer
    (free buffer)
    (fprintf stderr "Freed buffer\n"))
  (defer (fprintf stderr "I could have dependend on buffer in my defer\n"))
  (each-in-range 5 i
    (var-cast-to another-buffer (* char) (malloc 1024))
    (defer
      (free another-buffer)
      (fprintf stderr "Freed another buffer\n"))
    (when (= 2 i)
      (continue))
    (scope
     (defer (fprintf stderr "Exiting the scope surrounding 3\n"))
     (when (= 3 i)
       (defer (fprintf stderr "Hit 3, returning\n"))
       (return))))
  (when (and buffer (at 0 buffer))
    (fprintf stderr "%s\n" buffer)))

(defun main (&return int)
  (test-defer)
  (return 0))
