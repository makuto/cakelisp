(import &comptime-only "Macros.cake")
(c-import "<unordered_map>" "<vector>")
(c-import "DynamicLoader.hpp")

;;
;; Code management
;;
(def-type-alias FunctionReferenceArray (<> std::vector (* (* void))))
(def-type-alias FunctionReferenceMap (<> std::unordered_map std::string FunctionReferenceArray))
(def-type-alias FunctionReferenceMapIterator (in FunctionReferenceMap iterator))
(def-type-alias FunctionReferenceMapPair (<> std::pair (const std::string) FunctionReferenceArray))

(var registered-functions FunctionReferenceMap)

;;
;; Data/state management
;;
(def-type-alias StateVariableMap (<> std::unordered_map std::string (* void)))
(def-type-alias StateVariableMapIterator (in StateVariableMap iterator))

(var registered-state-variables StateVariableMap)

;;
;; Library management
;;
(var hot-reload-lib-path (* (const char)) "libGeneratedCakelisp.so")
(var hot-reload-lib-init-symbol-name (* (const char)) "libGeneratedCakelisp_initialize")
(var current-lib DynamicLibHandle nullptr)

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

(defun hot-reload-find-variable (name (* (const char)) variable-address-out (* (* void)) &return bool)
  (var find-it StateVariableMapIterator (on-call registered-state-variables find name))
  (unless (!= find-it (on-call registered-state-variables end))
    (set variable-address-out nullptr)
    (return false))
  (set (deref variable-address-out) (path find-it > second))
  (return true))

(defun hot-reload-register-variable (name (* (const char)) variable-address (* void))
  (set (at name registered-state-variables) variable-address))

(defun do-hot-reload (&return bool)
  (when current-lib
    (closeDynamicLibrary current-lib))
  (set current-lib (loadDynamicLibrary hot-reload-lib-path))
  (unless current-lib
    (return false))

  ;; Set up state variables. Each library knows about its own state variables
  (def-function-signature library-initialization-function-signature ())
  (var library-init-function library-initialization-function-signature
       (type-cast (getSymbolFromDynamicLibrary current-lib hot-reload-lib-init-symbol-name)
                  library-initialization-function-signature))
  (unless library-init-function
    (printf "error: Library missing intialization function. This is required for state variable support\n")
    (return false))
  (library-init-function)

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
