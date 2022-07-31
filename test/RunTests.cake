(add-cakelisp-search-directory "runtime")
(import "BuildTools.cake" "ComptimeHelpers.cake" "CHelpers.cake" "Cakelisp.cake")

;; We don't actually test anything here; we use comptime to run the tests
(defun main (&return int)
  (return 0))

(defun-comptime run-tests (manager (& ModuleManager) module (* Module) &return bool)
  (defstruct cakelisp-test
    test-name (* (const char))
    test-file (* (const char)))
  (var tests ([] (const cakelisp-test))
    (array
     (array "Hello" "test/Hello.cake")
     (array "Macros" "test/SimpleMacros.cake")
     (array "Code modification" "test/CodeModification.cake")
     ;; (array "Build options" "test/BuildOptions.cake")
     (array "Execute" "test/Execute.cake")
     (array "Export" "test/ExportTest.cake")
     (array "Defines" "test/Defines.cake")
     (array "Multi-line strings" "test/MultiLineStrings.cake")
     (array "Build helpers" "test/BuildHelpers.cake")
     (array "Hooks" "test/CompileTimeHooks.cake")
     (array "Build dependencies" "test/BuildDependencies.cake")
     (array "Cpp helpers" "test/CppHelpersTest.cake")
     (array "Tutorial: Basics" "test/Tutorial_Basics.cake")
     (array "Defer" "test/Defer.cake")))

  (var platform-config (* (const char))
    (comptime-cond
     ('Windows
      "runtime/Config_Windows.cake")
     (true
      "runtime/Config_Linux.cake")))

  (each-in-array tests i
    (var test-name (* (const char)) (field (at i tests) test-name))
    (var test-file (* (const char)) (field (at i tests) test-file))
    (var files ([] (* (const char))) (array platform-config test-file))
    (Logf "\n===============\n%s\n\n" test-name)
    (unless (cakelisp-evaluate-build-execute-files files (array-size files))
      (Logf "error: test %s failed\n" test-name)
      (return false))
    (Logf "\n%s succeeded\n" test-name))

  ;; Special cases that don't yet fit into the standard test loop
  (scope
   (Logf "\n===============\n%s\n\n" "Hot reloadable library")
   (var files ([] (* (const char)))
     (array platform-config "runtime/HotReloadingCodeModifier.cake" "runtime/TextAdventure.cake"))
   (unless (cakelisp-evaluate-build-files files (array-size files))
     (Logf "error: test %s failed\n" "Hot reloadable library")
     (return false))
   (Logf "\n%s succeeded\n" "Hot reloadable library"))

  (scope
   (Logf "\n===============\n%s\n\n" "Hot loader")
   (var files ([] (* (const char)))
     (array platform-config "runtime/HotLoader.cake"))
   (unless (cakelisp-evaluate-build-files files (array-size files))
     (Logf "error: test %s failed\n" "Hot loader")
     (return false))
   (Logf "\n%s succeeded\n" "Hot loader"))

  (Log "\nRunTests: All tests succeeded!\n")
  (return true))
(add-compile-time-hook-module pre-build run-tests)
