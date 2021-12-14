(comptime-define-symbol 'Windows)

;; TODO: Remove. These are only to ease testing via cross-compilation
(set-cakelisp-option build-time-compiler "/usr/bin/x86_64-w64-mingw32-g++")
(set-cakelisp-option build-time-compile-arguments
                     "-g" "-c" 'source-input "-o" 'object-output
                     'include-search-dirs 'additional-options)

(set-cakelisp-option compile-time-compiler "/usr/bin/x86_64-w64-mingw32-g++")
(set-cakelisp-option compile-time-compile-arguments
                     "-g" "-c" 'source-input "-o" 'object-output
                     'cakelisp-headers-include "-fPIC")

(set-cakelisp-option build-time-linker "/usr/bin/x86_64-w64-mingw32-g++")
(set-cakelisp-option build-time-link-arguments
                     "-o" 'executable-output 'object-input)

;; Use separate build configuration in case other things build files from src/
(add-build-config-label "CrossCompile_Windows")
