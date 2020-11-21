(c-import "Utilities.hpp")

(defstruct-local test-array-type
    commands ([] int))
(var my-list-of-lists ([] test-array-type) (array (array 1 2 3) (array 4 5 6)))

(defun main (&return int)
  (return 0))

(add-c-search-directory "src" "src" "notsrc")

(add-build-options "-Wall" "-Wextra" "-Wno-unused-parameter" "-O0")
