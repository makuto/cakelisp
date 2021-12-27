;; Use Cakelisp's RunProcess code during your project's runtime
;; Note: this code is GPLv3 because RunProcess is in src/, not runtime/
(import "BuildTools.cake") ;; For run-process macros

(export
 (c-import "RunProcess.hpp"))

(add-c-build-dependency "RunProcess.cpp")
