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

(defun-comptime pre-link-hook (manager (& ModuleManager)
                                       linkCommand (& ProcessCommand)
                                       linkTimeInputs (* ProcessCommandInput) numLinkTimeInputs int
                                       &return bool)
  (printf "Hello, Hot-reloading link!\n")
  ;; (on-call (field linkCommand arguments) push_back
           ;; (array ProcessCommandArgumentType_String "-lItWorked"))
  (return true))
(add-compile-time-hook pre-link pre-link-hook)

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

(defmacro state-variable-initializer-preamble ()
  (tokenize-push
   output
   (destructure-arguments name-index type-index assignment-index)
   (quick-token-at name name-index)
   (quick-token-at type type-index)
   (quick-token-at assignment assignment-index)

   (var init-function-name Token name)
   (var string-var-name Token name)
   (scope
    (var converted-name-buffer ([] 64 char) (array 0))
    ;; TODO: Need to pass this in somehow
    (var name-style NameStyleSettings)
    (lispNameStyleToCNameStyle (field name-style variableNameMode) (on-call (field name contents) c_str)
                               converted-name-buffer (sizeof converted-name-buffer) name)

    (var init-function-name-buffer ([] 256 char) (array 0))
    (PrintfBuffer init-function-name-buffer "%sInitialize" converted-name-buffer)
    (set (field init-function-name contents) init-function-name-buffer)

    (set (field string-var-name type) TokenType_String)))
  (return true))

(defmacro hot-reload-make-state-variable-initializer ()
  ;; Code that isn't hot-reloaded should be compatible if we do nothing
  (unless (field environment enableHotReloading)
    (return true))

  (state-variable-initializer-preamble)

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

;; Arrays must be handled specially because we need to know how big the array is to allocate it on
;; the heap. We also need to declare a way to get the array size (we don't know with the heap array)
(defmacro hot-reload-make-state-variable-array-initializer ()
  (state-variable-initializer-preamble)

  (unless (ExpectTokenType "hot-reload-make-state-variable-array-initializer"
                          type TokenType_OpenParen)
          (return false))

  (unless (= 0 (on-call (field (at (+ 1 type-index) tokens) contents) compare "[]"))
    (ErrorAtToken (at (+ 1 type-index) tokens)
                  "expected array type. Use hot-reload-make-state-variable-initializer instead")
    (return false))

  (var end-type-index int (FindCloseParenTokenIndex tokens type-index))
  (var last-argument-arg-index int (- (getNumArguments tokens type-index end-type-index) 1))
  (var last-argument-token-index int (getExpectedArgument "expected type"
                                      tokens type-index last-argument-arg-index end-type-index))
  (when (= -1 last-argument-token-index)
    (return false))
  (var type-strip-array (& (const Token)) (at last-argument-token-index tokens))

  (var array-length-name Token name)
  (var array-length-name-buffer ([] 256 char) (array 0))
  (PrintfBuffer array-length-name-buffer "%s-length" (on-call (field name contents) c_str))
  (set (field array-length-name contents) array-length-name-buffer)

  (var array-length-token Token assignment)
  (set (field array-length-token type) TokenType_Symbol)
  (var array-length int (- (getNumArguments tokens assignment-index
                                            (FindCloseParenTokenIndex tokens assignment-index))
                           1))
  (set (field array-length-token contents) (call (in std to_string) array-length))

  ;; Regardless of enableHotReloading, make array length available so that non-hot-reloaded code
  ;; still works when asking for the length
  (tokenize-push
   output
   (var-noreload (token-splice-ref array-length-name) (const int) (token-splice-ref array-length-token)))

  (when (field environment enableHotReloading)
    (tokenize-push
     output
     (defun-local (token-splice-ref init-function-name) ()
       (var existing-value (* void) nullptr)
       (if (hot-reload-find-variable (token-splice-ref string-var-name) (addr existing-value))
           (set (no-eval-var (token-splice-ref name)) (type-cast existing-value (* (token-splice-ref type-strip-array))))
           (block
               ;; C can have an easier time with plain old malloc and cast
               (set (no-eval-var (token-splice-ref name)) (new-array (token-splice-ref array-length-name)
                                                                     (token-splice-ref type-strip-array)))
             (var static-intializer-full-array (const (token-splice-ref type)) (token-splice-ref assignment))
             ;; Have to handle array initialization ourselves, because the state variable is just a pointer now
             (var current-elem int 0)
             (while (< current-elem (token-splice-ref array-length-name))
               (set (at current-elem (no-eval-var (token-splice-ref name)))
                    (at current-elem static-intializer-full-array))
               (incr current-elem))
             (hot-reload-register-variable (token-splice-ref string-var-name)
                                           (no-eval-var (token-splice-ref name))))))))
  (return true))

;; TODO: LEFT OFF: Why does this break the var->pointer auto deref? Does it happen too late? If so,
;; I must add symbol-level dependency resolution to fix
(defmacro state-var ()
  (destructure-arguments name-index type-index assignment-index)
  (quick-token-at name name-index)
  (quick-token-at type type-index)
  (quick-token-at assignment assignment-index)
  (tokenize-push
   output
   (var (token-splice-ref name) (token-splice-ref type) (token-splice-ref assignment)))
  (when (field environment enableHotReloading)
    (tokenize-push
     output
     (hot-reload-make-state-variable-initializer (token-splice-ref name) (token-splice-ref type)
                                                 (token-splice-ref assignment))))
  (return true))

;; TODO: Add type manipulations for array version
;; (defmacro state-var-array ()
;;   (destructure-arguments name-index type-index assignment-index)
;;   (quick-token-at name name-index)
;;   (quick-token-at type type-index)
;;   (quick-token-at assignment assignment-index)
;;   (tokenize-push
;;    (var (token-splice-ref name) (token-splice-ref type) (token-splice-ref assignment))
;;    (hot-reload-make-state-variable-initializer (token-splice-ref name) (token-splice-ref type)
;;                                                (token-splice-ref assignment)))
;;   (return true))

;;
;; Prospective compile-time modification
;;

(ignore
(defmacro state-var ()
  (quick-args var-name var-type &optional var-initializer)
  ;; Add regular variable declaration but with pointerized type, then add initializer function for
  ;; first time variable setup
  (tokenize-push (var (splice var-name) (* (splice var-type)) nullptr)
                 (defun (token-str-concat init- var-name) ()
                   (set (splice var-name) (splice var-initializer))))
  ;; Variable stored on the environment. This will auto (new) whatever my type is
  (with-comptime-variable-module state-vars (<> std::vector std::string))
  ;; Keep track of all state variables for later auto-dereferencing
  (on-call state-vars push_back (field var-name contents))
  (return true))

(state-var num-times-reloaded int 0)

(defun my-function ()
  (printf "Num times reloaded: %d\n" num-times-reloaded))

(defun-comptime state-var-auto-deref (definition-name (* (const char))
                                         definition-signature (& (const (<> std::vector Token)))
                                         definition-tags (& (const (<> std::vector DefinitionTags)))
                                         definition-body (& (const (<> std::vector Token)))
                                         definition-modification-out (& DefinitionModification))
  (when 'state-var-auto-deref in definition-tags
      ;; Already processed this function
        (return true))

  (var modified-definition (* (<> std::vector Token)) nullptr)

  (for-in token (const Token) definition-body
          (when (and (not (is-invocation token)) ;; Only pay attention to symbols
                     (not (= (field prev-token contents) "no-auto-deref")) ;; Prevent auto deref
                     (or (find state-vars (field token contents)) ;; Global or local state variable references
                         (find global-state-vars (field token contents))))
            (unless modified-definition
              (set modified-definition (new (<> std::vector Token)))
              ;; Catch up by copying the definition up to just before this point
              ;; (We only want to allocate memory if we're actually modifying the definition)
              (from definition-body to (- 1 token) (on-call modified-definition push_back prev-token)))
            ;; Push the state var, only with the auto deref
            (tokenize-push (addr (token-splice (addr token))))))

  (when modified-definition
    ;; The evaluator will check this and see it is changed. Subsequent modification functions will
    ;; get our modified version, not the old version
    (set (field definition-modification-out body) modified-definition)
    (on-call (field definition-modification-out tags) push_back 'state-var-auto-deref))

  (return true))

(add-compile-time-hook function-definition-evaluated state-var-auto-deref)
)
