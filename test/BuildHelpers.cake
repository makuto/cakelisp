(add-cakelisp-search-directory "runtime")
(import &comptime-only "BuildTools.cake")

(c-import "<stdio.h>")

(defun main (&return int)
  (printf "Hello, build tools!\n")
  (return 0))

(defun-comptime run-3rd-party-build (manager (& ModuleManager) module (* Module) &return bool)
  (run-process-make-arguments list-command "ls" "-lh" :in-directory "src")
  (var result bool (= 0 (run-process-wait-for-completion (addr list-command))))
  (return result))

(add-compile-time-hook-module pre-build run-3rd-party-build)
