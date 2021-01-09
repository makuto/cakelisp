(skip-build)

(ignore
;; Straightline version
(defun build-sdl (&return bool)
  (when (= target-platform 'Unix)
    (unless (file-exists "Dependencies/SDL/build/lib/libSDL2.so")
      (var sdl-build-dir (* (const char)) "Dependencies/SDL/build")
      (unless (make-directory sdl-build-dir)
        (return false))
      (var sdl-install-dir (* (const char)) (makeAbsolutePath_Allocated sdl-build-dir))
      (var sdl-install-prefix ([] MAX_PATH_LENGTH char) (array 0))
      (PrintfBuffer sdl-install-prefix "--prefix=%s/install" sdl-install-dir)
      (run-process "sh" "../configure" sdl-install-prefix
                   :in-directory sdl-build-dir (addr status))
      (waitForAllProcessesClosed)
      (unless (= 0 status)
        (return false))
      (run-process "make" :in-directory sdl-build-dir (addr status))
      (waitForAllProcessesClosed)
      (unless (= 0 status)
        (return false))
      (run-process "make" "install" :in-directory sdl-build-dir (addr status))
      (waitForAllProcessesClosed)
      (unless (= 0 status)
        (return false))
      (unless (file-exists "Dependencies/SDL/build/lib/libSDL2.so")
        (Log "SDL still not built in expected location, despite build success. Configuration error?")
        (return false)))))

;; Fancy version: DSL
;; Doesn't actually add that much over just adding a run-process-sequential macro
;; Another helpful macro: (create-and-format-string build-dir MAX_PATH_LENGTH "%s/build" sdl-dir)
(defun build-sdl-dsl (&return bool)
  (unless (file-exists "Dependencies/SDL")
    (Log "Failed to find SDL in Dependencies/SDL.\nIf you are using Git, run:\n
    git submodule update --init --recursive\n
If you are not using Git, please download SDL via hg:\n
    hg clone http://hg.libsdl.org/SDL\n
Or download a source release from https://www.libsdl.org/hg.php\n
Put it in Dependencies/SDL")
    (return false))

  (unless (file-exists "Dependencies/SDL/build/lib/libSDL2.so")
    (make-directory build-dir)
    (build-process "SDL (Unix, CMake, Ninja)"
                   (sequential
                    (run-process "sh" "../configure" sdl-install-prefix :in-directory build-dir
                                 :on-fail "could not configure SDL. Is autotools installed?")
                    (run-process "make" :in-directory build-dir)
                    (run-process "make" "install" :in-directory build-dir))))

  (unless (file-exists "Dependencies/SDL/build/lib/libSDL2.so")
    (Log "did not find required files after successful SDL build. Does configuration need updating?")
    (return false))
  (return true))

(make-directory "Dependencies/ogre-next/build")
(run-process "cmake" ".." :in-directory "Dependencies/ogre-next/build" (addr status))
(waitForAllProcessesClosed)
(unless (= 0 status)
  (return false))
(run-process "ninja" :in-directory "Dependencies/ogre-next/build" (addr status))
(waitForAllProcessesClosed)
(unless (= 0 status)
  (return false))
) ;; End ignore

;; Returns exit code (0 = success)
(defun-comptime run-process-wait-for-completion (run-arguments (* RunProcessArguments)
                                                               &return int)
  (var status int -1)
  (unless (= 0 (runProcess (deref run-arguments) (addr status)))
    (Log "error: failed to run process\n")
    (return 1))

  (waitForAllProcessesClosed nullptr)
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
           ((= 0 (on-call (path current-token > contents) compare ":in-directory"))
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
                  (on-call (token-splice-addr working-dir-str-var) c_str)))

            ;; Absorb src for incr
            (set current-token next-token))

           (true
            (ErrorAtToken (deref current-token) "unrecognized specifier. Valid specifiers: :in-directory")
            (return false))))

        ;; Everything else is a argument to the command
        (true
         (on-call command-arguments push_back (deref current-token))))
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
     (Logf "error: failed to resolved executable %s. Is it installed?\\n"
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
