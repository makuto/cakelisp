(set-cakelisp-option enable-hot-reloading true)

(set-module-option build-time-compiler "/usr/bin/clang++")
;; Include cakelisp source for DynamicLoader.hpp
(set-module-option build-time-compile-arguments
                   "-std=c++11" "-Wall" "-Wextra" "-Wno-unused-parameter" "-O0"
                   ;; TODO: Multiplatform support
                   "-DUNIX"
                   "-g" "-c" 'source-input "-o" 'object-output "-fPIC" "-Isrc/")
;; TODO: This only makes sense on a per-target basis. Instead, modules should be able to append
;; arguments to the link command only
(set-cakelisp-option build-time-linker "/usr/bin/clang++")
;; This needs to link -ldl and such (depending on platform...)
(set-cakelisp-option build-time-link-arguments
                     "-shared" "-o" 'executable-output 'object-input
                     ;; TODO: OS dependent
                     "-ldl" "-lpthread")

;; TODO: Relative vs. absolute paths
(add-cpp-build-dependency "../src/DynamicLoader.cpp")

(defun-comptime pre-build-hook (manager (& ModuleManager) module (* Module) &return bool)
  (printf "Hello, Hot-reloading build!\n")
  (return true))
(add-compile-time-hook-module pre-build pre-build-hook)

(import &comptime-only "Macros.cake")
(c-import "<unordered_map>" "<vector>")
(c-import "DynamicLoader.hpp")

(def-type-alias FunctionReferenceArray (<> std::vector (* (* void))))
(def-type-alias FunctionReferenceMap (<> std::unordered_map std::string FunctionReferenceArray))
(def-type-alias FunctionReferenceMapIterator (in FunctionReferenceMap iterator))
(def-type-alias FunctionReferenceMapPair (<> std::pair (const std::string) FunctionReferenceArray))

(var registered-functions FunctionReferenceMap)

(var current-lib DynamicLibHandle nullptr)

(var hot-reload-lib-path (* (const char)) "libGeneratedCakelisp.so")

(defun register-function-pointer (function-pointer (* (* void))
                                  function-name (* (const char)))
  (var findIt FunctionReferenceMapIterator
       (on-call registered-functions find function-name))
  (if (= findIt (on-call registered-functions end))
      (block
          (var new-function-pointer-array FunctionReferenceArray)
        (on-call new-function-pointer-array push_back function-pointer)
        (set (at function-name registered-functions)
             ;; This could also be written as (std::move new-function-pointer-array)
             ;; I'm not sure how I want it to work
             (call (in std move) new-function-pointer-array)))
      (on-call (path findIt > second) push_back function-pointer)))

(defun do-hot-reload (&return bool)
  (when current-lib
    (closeDynamicLibrary current-lib))
  (set current-lib (loadDynamicLibrary hot-reload-lib-path))
  (unless current-lib
    (return false))

  (for-in function-referent-it (& FunctionReferenceMapPair) registered-functions
          (var loaded-symbol (* void)
               (getSymbolFromDynamicLibrary current-lib
                                            (on-call (path function-referent-it . first) c_str)))
          (unless loaded-symbol
            (return false))
          ;; TODO: What will happen once modules are unloaded? We can't store pointers to their static memory
          (for-in function-pointer (* (* void)) (path function-referent-it . second)
                  (set (deref function-pointer) loaded-symbol)))
  (return true))
