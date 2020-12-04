(import &comptime-only "Macros.cake")
(c-import "<unordered_map>" "<vector>")
(c-import "DynamicLoader.hpp")

;;
;; Function management
;;

(def-type-alias FunctionReferenceArray (<> std::vector (* (* void))))
(def-type-alias FunctionReferenceMap (<> std::unordered_map std::string FunctionReferenceArray))
(def-type-alias FunctionReferenceMapIterator (in FunctionReferenceMap iterator))
(def-type-alias FunctionReferenceMapPair (<> std::pair (const std::string) FunctionReferenceArray))

(var registered-functions FunctionReferenceMap)

(var current-lib DynamicLibHandle null)

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

  ;; Intialize variables
  (var global-initializer (* void)
       (getSymbolFromDynamicLibrary current-lib "hotReloadInitializeState"))
  (if global-initializer
      (block
          (def-function-signature global-initializer-signature ())
        (call (type-cast global-initializer global-initializer-signature)))
      (printf "warning: global initializer 'hotReloadInitializeState' not found!"))

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

;;
;; Data/state management
;;

(def-type-alias StateVariableMap (<> std::unordered_map std::string (* void)))
(def-type-alias StateVariableMapIterator (in StateVariableMap iterator))

(var registered-state-variables StateVariableMap)
(var verbose-variables bool false)

(defun hot-reload-find-variable (name (* (const char)) variable-address-out (* (* void)) &return bool)
  (var find-it StateVariableMapIterator (on-call registered-state-variables find name))
  (unless (!= find-it (on-call registered-state-variables end))
    (set variable-address-out nullptr)
    (when verbose-variables
      (printf "Did not find variable %s\n" name))
    (return false))
  (when verbose-variables
    (printf "Found variable %s at %p\n" name (path find-it > second)))
  (set (deref variable-address-out) (path find-it > second))
  (return true))

;; TODO: Free variables. They'll need generated destructors for C++ types (see compile-time vars)
(defun hot-reload-register-variable (name (* (const char)) variable-address (* void))
  (set (at name registered-state-variables) variable-address))

;;
;; Building
;;

;; TODO: This only makes sense on a per-target basis. Instead, modules should be able to append
;; arguments to the link command only
(set-cakelisp-option build-time-linker "/usr/bin/clang++")
;; This needs to link -ldl and such (depending on platform...)
(set-cakelisp-option build-time-link-arguments
                     ;; "-shared" ;; This causes c++ initializers to fail and no linker errors. Need to only enable on lib?
                     "-o" 'executable-output 'object-input
                     ;; TODO: OS dependent
                     ;; Need --export-dynamic so the loaded library can use our symbols
                     "-ldl" "-lpthread" "-Wl,-rpath,.,--export-dynamic")

(add-build-options
 "-DUNIX"
 "-fPIC")

;; Include cakelisp source for DynamicLoader.hpp
(add-c-search-directory module "src")
(add-cpp-build-dependency "DynamicLoader.cpp")
