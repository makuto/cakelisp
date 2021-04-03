;; (skip-build)

(add-cakelisp-search-directory "runtime")
(import &comptime-only "BuildTools.cake" "Macros.cake" "CHelpers.cake")

;; We don't actually test anything here; we use comptime to run the tests
(defun main (&return int)
  (return 0))

(defun-comptime run-tests (manager (& ModuleManager) module (* Module) &return bool)
  (var cakelisp-executable (* (const char)) "./bin/cakelisp")
  (comptime-cond
   ('Windows
    (set cakelisp-executable "./bin/cakelisp.exe"))
   ('Unix)
   (true
    (comptime-error "Please specify platform to run tests. You can do this by including
 runtime/Config_[YOUR PLATFORM].cake before this file on the command, e.g.:
 cakelisp runtime/Config_Linux.cake test/RunTests.cake")))

  (defstruct cakelisp-test
    test-name (* (const char))
    test-file (* (const char)))
  (var tests ([] (const cakelisp-test))
       (array
        (array "Code modification" "test/CodeModification.cake")
        ;; (array "Build options" "test/BuildOptions.cake")
        (array "Execute" "test/Execute.cake")
        (array "Defines" "test/Defines.cake")
        (array "Multi-line strings" "test/MultiLineStrings.cake")
        (array "Build helpers" "test/BuildHelpers.cake")
        (array "Hooks" "test/CompileTimeHooks.cake")))

  (var platform-config (* (const char))
       (comptime-cond
        ('Windows
         "runtime/Config_Windows.cake")
        (true
         "runtime/Config_Linux.cake")))

  (var i int 0)
  (while (< i (array-size tests))
    (var test-name (* (const char)) (field (at i tests) test-name))
    (var test-file (* (const char)) (field (at i tests) test-file))
    (Logf "\n===============\n%s\n\n" test-name)
    (run-process-sequential-or
     (cakelisp-executable
      "--execute"
      platform-config test-file)
     (Logf "error: test %s failed\n" test-name)
     (return false))
    (Logf "\n%s succeeded\n" test-name)
    (incr i))
  (Log "\nAll tests succeeded!\n")
  (return true))
(add-compile-time-hook-module pre-build run-tests)
