;; Cakelisp.el - a derived major mode for editing Cakelisp files
;; GPL-3.0-or-later

(define-derived-mode cakelisp-mode
    lisp-mode "Cakelisp"
    "Major mode for Cakelisp"
    (setq lisp-indent-function 'common-lisp-indent-function) ;; Fixes argument lists

    (put 'defun-local 'lisp-indent-function 'defun)
    (put 'defun-comptime 'lisp-indent-function 'defun)
    (put 'defun-nodecl 'lisp-indent-function 'defun)
    (put 'defgenerator 'lisp-indent-function 'defun)
    (put 'def-function-signature 'lisp-indent-function 'defun)
    (put 'def-function-signature-global 'lisp-indent-function 'defun)

    (put 'defstruct-local 'lisp-indent-function 1)
    (put 'defstruct 'lisp-indent-function 1)

    (put 'var 'lisp-indent-function 2)
    (put 'var-static 'lisp-indent-function 2)
    (put 'var-global 'lisp-indent-function 2)
    ;;(put 'block 'lisp-indent-function 3) ;; Doesn't work because block is special (always expects 1)

    (put 'tokenize-push 'lisp-indent-function 1)
    (put 'for-in 'lisp-indent-function 3)

    ;; Macros
    (put 'each-in-range 'lisp-indent-function 2)
    (put 'each-in-interval 'lisp-indent-function 3)
    (put 'each-in-closed-interval-descending 'lisp-indent-function 3)
    (put 'each-in-array 'lisp-indent-function 2)
    (put 'each-char-in-string 'lisp-indent-function 2)
    (put 'each-char-in-string-const 'lisp-indent-function 2)
    (put 'c-for 'lisp-indent-function 3)

    (put 'run-process-sequential-or 'lisp-indent-function 1)
    (put 'runtime-run-process-sequential-or 'lisp-indent-function 1)
    (put 'runtime-run-process-sequential-with-output-or 'lisp-indent-function 2)
    (put 'run-process-start-or 'lisp-indent-function 2)

    (put 'if-c-preprocessor-defined 'lisp-indent-function 1)

    (put 'each-token-argument-in 'lisp-indent-function 4)

    (put 'defenum 'lisp-indent-function 1)
    (put 'defenum-local 'lisp-indent-function 1)

    (put 'defer 'lisp-indent-function 0)

    ;; Keywords
    ;; "(def[a-zA-Z0-9-]*" all define keywords

    ;; Configuration, build stuff, etc.
    (font-lock-add-keywords nil '(("(\\(add-build-config-label\\|add-build-options\\|add-build-options-global\\|add-c-build-dependency\\|add-c-search-directory-global\\|add-c-search-directory-module\\|add-cakelisp-search-directory\\|add-compile-time-hook\\|add-compile-time-hook-module\\|add-compiler-link-options\\|add-cpp-build-dependency\\|add-library-dependency\\|add-library-runtime-search-directory\\|add-library-search-directory\\|add-linker-options\\|add-static-link-objects\\|set-cakelisp-option\\|set-module-option\\|c-import\\|c-preprocessor-define\\|c-preprocessor-define-global\\|comptime-cond\\|comptime-define-symbol\\|comptime-error\\|import\\|rename-builtin\\|export\\|export-and-evaluate\\|splice-point\\|token-splice\\|token-splice-addr\\|token-splice-array\\|token-splice-rest\\|tokenize-push\\)[ )\n]"
                                   1 font-lock-builtin-face)))

    (font-lock-add-keywords nil '(("\\b\\(true\\|false\\|null\\)\\b"
                                   1 font-lock-builtin-face)))

    (font-lock-add-keywords nil '(("(\\(addr\\|and\\|array\\|at\\|bit-<<\\|bit->>\\|bit-and\\|bit-ones-complement\\|bit-or\\|bit-xor\\|call\\|call-on\\|call-on-ptr\\|decr\\|def-function-signature\\|def-function-signature-global\\|def-type-alias\\|def-type-alias-global\\|defgenerator\\|defmacro\\|defstruct\\|defstruct-local\\|defun\\|defun-comptime\\|defun-local\\|defun-nodecl\\|delete\\|delete-array\\|deref\\|eq\\|field\\|in\\|incr\\|mod\\|neq\\|new\\|new-array\\|not\\|nth\\|or\\|path\\|scope\\|set\\|type\\|type-cast\\|var\\|var-global\\|var-static\\)[ )\n]"
                                   1 font-lock-keyword-face)))

    ;; Control flow
    (font-lock-add-keywords nil '(("(\\(break\\|cond\\|continue\\|for-in\\|if\\|return\\|unless\\|when\\|while\\)[ )\n]"
                                   1 font-lock-keyword-face)))

    (font-lock-add-keywords nil '(("(\\(ignore\\)[ )\n]"
                                   ;; So you know it's not running; comment-face would be ideal
                                   1 font-lock-warning-face))))

(add-to-list 'auto-mode-alist '("\\.cake?\\'" . cakelisp-mode))
(add-to-list 'auto-mode-alist '("\\.cakedata?\\'" . cakelisp-mode))
