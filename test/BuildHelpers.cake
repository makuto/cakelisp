(add-cakelisp-search-directory "runtime")
(import &comptime-only "BuildTools.cake")

(c-import "<stdio.h>")

(defun main (&return int)
  (printf "Hello, build tools!\n")
  (return 0))

(defun-comptime run-3rd-party-build (manager (& ModuleManager) module (* Module) &return bool)
  ;; Sequential
  (run-process-sequential-or ("ls" "-lh" :in-directory "src")
                             (Log "failed to run process")
                             (return false))
  ;; Parallel
  (var ls-status int -1)
  (run-process-start-or (addr ls-status) ("ls" "-lh")
                     (return false))
  (var echo-status int -1)
  (run-process-start-or (addr echo-status) ("echo" "Hello multiprocessing!")
                     (return false))
  (waitForAllProcessesClosed null)
  (return (and (= ls-status 0) (= echo-status 0))))

(add-compile-time-hook-module pre-build run-3rd-party-build)
