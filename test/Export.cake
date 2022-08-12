(export
 (add-cakelisp-search-directory "runtime")
 (import "CHelpers.cake"))

(export
 (declare-extern-function test ())
 (add-cpp-build-dependency "dir/Test.cpp"))

(set-cakelisp-option executable-output "test/Export")
