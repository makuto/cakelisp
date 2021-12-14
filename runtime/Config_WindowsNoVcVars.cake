;; This file serves as a reference for building without needing VCVars, because we specify everything ourselves instead.
;; TODO: Remove this and build it into the system via FindVisualStudio.
;; Example:
;; bin/cakelisp.exe --ignore-cache --execute runtime/Config_Windows.cake runtime/Config_WindowsNoVcVars.cake test/SimpleMacros.cake

(comptime-define-symbol 'Windows)

;; TODO: Get these paths from FindVisualStudio
(set-cakelisp-option compile-time-compiler "cl.exe")
(set-cakelisp-option compile-time-compile-arguments
                     "/nologo" "/DEBUG:FASTLINK" "/MDd" "/c" 'source-input 'object-output
                     'cakelisp-headers-include
                     "/X" ;; Ignore standard includes
                     ;; Obtained via echo %INCLUDE% in a Visual Studio Developer Command Prompt
                     "/I" #"#"C:\Program Files (x86)\Microsoft Visual Studio\2019\Community\VC\Tools\MSVC\14.29.30133\include"#"#
                     "/I" #"#"C:\Program Files (x86)\Windows Kits\10\include\10.0.19041.0\ucrt"#"#
                     ;; "/I" #"#"C:\Program Files (x86)\Windows Kits\10\include\10.0.19041.0\shared"#"#
                     ;; "/I" #"#"C:\Program Files (x86)\Windows Kits\10\include\10.0.19041.0\um"#"#
                     ;; "/I" #"#"C:\Program Files (x86)\Windows Kits\10\include\10.0.19041.0\winrt"#"#
                     ;; "/I" #"#"C:\Program Files (x86)\Windows Kits\10\include\10.0.19041.0\cppwinrt"#"#
                     )

(set-cakelisp-option compile-time-linker "link.exe")
(set-cakelisp-option compile-time-link-arguments
                     "/nologo" "/DLL" "/LIBPATH:bin" "cakelisp.lib" "/DEBUG:FASTLINK" 'library-output 'object-input
                     ;; From echo %LIB%
                     ;; #"#/LIBPATH:"C:\Program Files (x86)\Microsoft Visual Studio\2019\Community\VC\Tools\MSVC\14.29.30133\ATLMFC\lib\x64"#"#
                     #"#/LIBPATH:"C:\Program Files (x86)\Microsoft Visual Studio\2019\Community\VC\Tools\MSVC\14.29.30133\lib\x64"#"#
                     #"#/LIBPATH:"C:\Program Files (x86)\Windows Kits\10\lib\10.0.19041.0\ucrt\x64"#"#
                     #"#/LIBPATH:"C:\Program Files (x86)\Windows Kits\10\lib\10.0.19041.0\um\x64"#"#
                     'import-library-paths
                     'import-libraries)

(add-c-search-directory-global #"#C:\Program Files (x86)\Microsoft Visual Studio\2019\Community\VC\Tools\MSVC\14.29.30133\include#"#
                               #"#C:\Program Files (x86)\Windows Kits\10\include\10.0.19041.0\ucrt#"#)

(add-linker-options
 #"#/LIBPATH:"C:\Program Files (x86)\Microsoft Visual Studio\2019\Community\VC\Tools\MSVC\14.29.30133\lib\x64"#"#
 #"#/LIBPATH:"C:\Program Files (x86)\Windows Kits\10\lib\10.0.19041.0\ucrt\x64"#"#
 #"#/LIBPATH:"C:\Program Files (x86)\Windows Kits\10\lib\10.0.19041.0\um\x64"#"#)
