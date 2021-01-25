(c-import "<stdio.h>")

(defun main(&return int)
  (printf "Hello, world! From Cakelisp!\n")
  (return 0))

(add-library-search-directory "test/search/me")
(add-library-runtime-search-directory "test/runtime/search/me" "me/too")
(add-library-dependency "test" "test2")
(add-library-dependency "test3")
(add-linker-options "-shared")
