;; Build Tools - useful macros/functions for compile-time code
;; These rely on Cakelisp, so don't expect it to work outside comptime

(skip-build)

;; Returns exit code (0 = success)
(defun-comptime run-process-wait-for-completion (run-arguments (* RunProcessArguments)
                                                               &return int)
  (var status int -1)
  (unless (= 0 (runProcess (deref run-arguments) (addr status)))
    (Log "error: failed to run process\n")
    (return 1))

  (waitForAllProcessesClosed null)
  (return status))

(defmacro gen-unique-symbol (token-var-name symbol prefix string reference-token any)
  (tokenize-push
   output
   (var (token-splice token-var-name) Token (token-splice reference-token))
   (MakeContextUniqueSymbolName environment context (token-splice prefix)
                                (addr (token-splice token-var-name))))
  (return true))

;; Creates a variable arguments-out-name set up to run the given process
;; Use :in-directory to specify the working directory to run the process in
(defmacro run-process-make-arguments (arguments-out-name symbol executable-name any
                                      &optional &rest arguments any)
  (var specifier-tokens (<> std::vector Token))
  (var command-arguments (<> std::vector Token))

  (when arguments
    (var current-token (* (const Token)) arguments)
    (var end-paren-index int (FindCloseParenTokenIndex tokens startTokenIndex))
    (var end-token (* (const Token)) (addr (at end-paren-index tokens)))
    (while (< current-token end-token)
      (cond
        ;; Special symbols to add optional specifications
        ((and (= TokenType_Symbol (path current-token > type))
              (isSpecialSymbol (deref current-token)))
         (cond
           ((= 0 (call-on compare (path current-token > contents) ":in-directory"))
            (var next-token (* (const Token)) (+ 1 current-token))
            (unless (< next-token end-token)
              (ErrorAtToken (deref next-token) "expected expression for working directory")
              (return false))

            (gen-unique-symbol working-dir-str-var "working-dir-str" (deref next-token))

            (tokenize-push
             specifier-tokens
             (var (token-splice-addr working-dir-str-var) (in std string) (token-splice next-token))
             ;; I thought I needed to make it absolute, but at least on Linux, chdir works with relative
             ;; TODO: Remove this if Windows is fine with it as well
             ;; (scope ;; Make the path absolute if necessary
             ;;  (var working-dir-alloc (* (const char)) (makeAbsolutePath_Allocated null (token-splice next-token)))
             ;;  (unless working-dir-alloc
             ;;    (Logf "error: could not find expected directory %s" (token-splice next-token))
             ;;    (return false))
             ;;  ;; Copy it so we don't need to worry about freeing if something goes wrong
             ;;  (set (token-splice-addr working-dir-str-var) working-dir-alloc)
             ;;  (free (type-cast working-dir-alloc (* void))))
             (set (field (token-splice arguments-out-name) working-directory)
                  (call-on c_str (token-splice-addr working-dir-str-var))))

            ;; Absorb src for incr
            (set current-token next-token))

           (true
            (ErrorAtToken (deref current-token) "unrecognized specifier. Valid specifiers: :in-directory")
            (return false))))

        ;; Everything else is a argument to the command
        (true
         (call-on push_back command-arguments (deref current-token))))
      (incr current-token)))

  (gen-unique-symbol resolved-executable-var "resolved-executable" (deref executable-name))
  (gen-unique-symbol command-array-var "command-arguments"
                     (? arguments (deref arguments) (deref executable-name)))

  (tokenize-push
   output
   (var (token-splice arguments-out-name) RunProcessArguments (array 0))
   (var (token-splice-addr resolved-executable-var) ([] MAX_PATH_LENGTH char) (array 0))
   (unless (resolveExecutablePath (token-splice executable-name)
                                  (token-splice-addr resolved-executable-var)
                                  (sizeof (token-splice-addr resolved-executable-var)))
     (Logf "error: failed to resolve executable %s. Is it installed? Is the environment/path " \
           "configured correctly?\n"
           (token-splice executable-name))
     (return false))
   (set (field (token-splice arguments-out-name) fileToExecute)
        (token-splice-addr resolved-executable-var))
   (token-splice-array specifier-tokens)
   (var (token-splice-addr command-array-var) ([] (* (const char)))
        (array (token-splice-addr resolved-executable-var)
               (token-splice-array command-arguments) null))
   (set (field (token-splice arguments-out-name) arguments)
        (token-splice-addr command-array-var)))
  (return true))

;; Sequential means this will block until the process completes. If it fails, on-failure block will
;; execute.
(defmacro run-process-sequential-or (command array &rest on-failure array)
  (var my-tokens (<> std::vector Token))
  (tokenize-push
   output
   (scope
    (run-process-make-arguments process-command
                                ;; +1 because we want the inside of the command
                                (token-splice-rest (+ 1 command) tokens))
    (unless (= 0 (run-process-wait-for-completion (addr process-command)))
      (token-splice-rest on-failure tokens))))
  (return true))

;; status-int-ptr should be an address to an int variable which can be checked for process exit
;; code, but only after waitForAllProcessesClosed
;; TODO: Make run-process-start-or block based on number of cores?
(defmacro run-process-start-or (status-int-ptr any command array &rest on-failure-to-start array)
  (var my-tokens (<> std::vector Token))
  (tokenize-push
   output
   (scope
    (run-process-make-arguments process-command
                                ;; +1 because we want the inside of the command
                                (token-splice-rest (+ 1 command) tokens))
    (unless (= 0 (runProcess process-command (token-splice status-int-ptr)))
     (Log "error: failed to start process\n")
     (token-splice-rest on-failure-to-start tokens))))
  (return true))
