(skip-build)
(export
 (add-cakelisp-search-directory "runtime")
 (import &comptime-only "CHelpers.cake"))

(export
 (declare-extern-function test ())
 (add-cpp-build-dependency "dir/Test.cpp"))
