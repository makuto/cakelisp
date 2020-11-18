(set-cakelisp-option enable-hot-reloading true)

;; TODO: If this calls a function which needs var, that's a circular dependency
;; Silly example, but shows user can replace built-in with a custom macro
;; Would this be better as a code-modification thing?
(rename-builtin "var" "badvar")
(defmacro var ()
  ;; Var cannot be used within var, because it's undefined. This excludes a lot of macros
  ;; (get-or-create-comptime-var var-replacements (<> std::vector (* (const Token))))
  ;; (for-in replaced-token (* (const Token)) (addr var-replacements)
          ;; (NoteAtToken (deref replaced-token) "Replaced already"))

  (PushBackTokenExpression output (addr (at startTokenIndex tokens)))
  ;; TODO: This is no good, because var looks at its invocation name
  (set (field (at 1 output) contents) "badvar")
  (return true))

(set-cakelisp-option compile-time-compiler "/usr/bin/clang++")
;; Include cakelisp source for DynamicLoader.hpp
(set-cakelisp-option compile-time-compile-arguments
                   ;; Precompiled headers give a ~60% speedup. See https://clang.llvm.org/docs/UsersManual.html#precompiled-headers
                   ;; "-include" "src/Evaluator.hpp"
                   "-Wall" "-Wextra" "-Wno-unused-parameter" "-O0"
                   ;; TODO: Multiplatform support
                   "-DUNIX"
                   "-g" "-c" 'source-input "-o" 'object-output "-fPIC" "-Isrc/")

(set-module-option build-time-compiler "/usr/bin/clang++")
;; Include cakelisp source for DynamicLoader.hpp
(set-module-option build-time-compile-arguments
                   "-Wall" "-Wextra" "-Wno-unused-parameter" "-O0"
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
