(skip-build)
(set-cakelisp-option enable-hot-reloading true)

(import &comptime-only "Macros.cake")

(defun-comptime make-code-hot-reloadable (environment (& EvaluatorEnvironment)
                                                      was-code-modified (& bool)
                                                      &return bool)
  (return true))

(add-compile-time-hook post-references-resolved make-code-hot-reloadable)

;; ;; TODO: This only makes sense on a per-target basis. Instead, modules should be able to append
;; ;; arguments to the link command only
;; (set-cakelisp-option build-time-linker "/usr/bin/clang++")
;; ;; This needs to link -ldl and such (depending on platform...)
;; (set-cakelisp-option build-time-link-arguments
;;                      ;; "-shared" ;; This causes c++ initializers to fail and no linker errors. Need to only enable on lib?
;;                      "-o" 'executable-output 'object-input)

(defmacro command-add-string-argument ()
  (destructure-arguments new-argument-index)
  (quick-token-at new-argument new-argument-index)
  (tokenize-push output (on-call (field linkCommand arguments) push_back
                                 (array ProcessCommandArgumentType_String
                                        (token-splice (addr new-argument)))))
  (return true))

(defun-comptime hot-reload-lib-link-hook (manager (& ModuleManager)
                                                  linkCommand (& ProcessCommand)
                                                  linkTimeInputs (* ProcessCommandInput) numLinkTimeInputs int
                                                  &return bool)
  (command-add-string-argument "-shared")
  ;; (command-add-string-argument "--export-all-symbols")
  (return true))

(add-compile-time-hook pre-link hot-reload-lib-link-hook)

;; TODO: Automatically make library if no main found
(set-cakelisp-option executable-output "libGeneratedCakelisp.so")
