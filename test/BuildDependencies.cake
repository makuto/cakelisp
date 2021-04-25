(add-cakelisp-search-directory "runtime")
(import &comptime-only "CHelpers.cake")

(declare-extern-function test ())

(defun main (&return int)
  (test)
  (return 0))

(add-c-build-dependency "Test.cpp")

(add-c-search-directory-module "test/dir")
