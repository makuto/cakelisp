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

(var-noreload registered-functions FunctionReferenceMap)

;;
;; Data/state management
;;
(def-type-alias StateVariableMap (<> std::unordered_map std::string (* void)))
(def-type-alias StateVariableMapIterator (in StateVariableMap iterator))

(var-noreload registered-state-variables StateVariableMap)

;;
;; Library management
;;
(var-noreload hot-reload-lib-path (* (const char)) "libGeneratedCakelisp.so")
(var-noreload hot-reload-lib-init-symbol-name (* (const char)) "libGeneratedCakelisp_initialize")
(var-noreload current-lib DynamicLibHandle nullptr)

(var-noreload verbose-variables bool true)

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
    (when verbose-variables
      (printf "Did not find variable %s\n" name))
    (return false))
  (when verbose-variables
    (printf "Found variable %s at %p\n" name (path find-it > second)))
  (set (deref variable-address-out) (path find-it > second))
  (return true))

;; TODO: Free variables
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
    (printf
     "error: Library missing initialization function. This is required for state variable support\n")
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

(defmacro hot-reload-make-state-variable-initializer ()
  (destructure-arguments name-index type-index assignment-index)
  (quick-token-at name name-index)
  (quick-token-at type type-index)
  (quick-token-at assignment assignment-index)

  (var converted-name-buffer ([] 64 char) (array 0))
  ;; TODO: Need to pass this in somehow
  (var name-style NameStyleSettings)
  (lispNameStyleToCNameStyle (field name-style variableNameMode) (on-call (field name contents) c_str)
                             converted-name-buffer (sizeof converted-name-buffer) name)

  (var init-function-name Token name)
  (var init-function-name-buffer ([] 256 char) (array 0))
  (PrintfBuffer init-function-name-buffer "%sInitialize" converted-name-buffer)
  (set (field init-function-name contents) init-function-name-buffer)

  (var string-var-name Token name)
  (set (field string-var-name type) TokenType_String)

  (tokenize-push
   output
   (defun-local (token-splice-ref init-function-name) ()
     (var existing-value (* void) nullptr)
     (if (hot-reload-find-variable (token-splice-ref string-var-name) (addr existing-value))
         (set (no-eval-var (token-splice-ref name)) (type-cast existing-value (* (token-splice-ref type))))
         (block
             ;; C can have an easier time with plain old malloc and cast
             (set (no-eval-var (token-splice-ref name)) (new (token-splice-ref type)))
           (set (token-splice-ref name) (token-splice-ref assignment))
           (hot-reload-register-variable (token-splice-ref string-var-name)
                                         (no-eval-var (token-splice-ref name)))))))
  (return true))

;; (defun-local currentRoomInitialize ()
;;   (var existing-value (* void )nullptr )
;;   (if (hot-reload-find-variable "current-room" (addr existing-value ))
;;       (set (no-eval-var current-room ) (type-cast existing-value (* (* (const room ))nullptr )))
;;       (block (set (no-eval-var current-room )(new (* (const room ))nullptr ))
;;         (set current-room 0 )
;;         (hot-reload-register-variable "current-room" (no-eval-var current-room )))))
