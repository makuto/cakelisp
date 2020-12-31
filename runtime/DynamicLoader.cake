;; Dynamically load code from shared objects (.so) or dynamic libraries (.dll)
;; This code was based on cakelisp/src/DynamicLoader.cpp, which is GPL, but I wanted runtime to be
;; MIT, so this copy is justified (this comment is by Macoy Madson, so I can change the license)
(c-import "<stdio.h>" "<string>" "<unordered_map>")

(comptime-cond
 ('Unix
  (c-import "<dlfcn.h>"))
 ('Windows
  ;; TODO: This is pretty annoying, because I'll need to add &with-decls etc.
  (c-preprocessor-define WIN32_LEAN_AND_MEAN)
  (c-import "<windows.h>"))
 (true
  ;; If you're hitting this, you may need to port this over to whatever new platform you are on
  (comptime-error
   "This module requires platform-specific code. Please define your platform before importing this module, e.g.: (comptime-define-symbol 'Unix). Supported platforms: 'Unix, 'Windows")))

(c-preprocessor-define MAX_PATH_LENGTH 256)

(def-type-alias-global DynamicLibHandle (* void))

(defstruct DynamicLibrary
  handle DynamicLibHandle)

(def-type-alias DynamicLibraryMap (<> std::unordered_map std::string DynamicLibrary))
(var dynamicLibraries DynamicLibraryMap)

(defun-local makeBackslashFilename (buffer (* char) bufferSize
                                          int filename (* (const char)))
  (var bufferWrite (* char) buffer)
  (var currentChar (* (const char)) filename)
  (while (deref currentChar)
    (if (= (deref currentChar)  '/')
        (set (deref bufferWrite) '\\')
        (set (deref bufferWrite) (deref currentChar)))

    (incr bufferWrite)
    (incr currentChar)
    (when (>= (- bufferWrite buffer) bufferSize)
      (fprintf stderr "error: could not make safe filename: buffer too small\n")
      (break))))

(defun loadDynamicLibrary (libraryPath (* (const char))  &return DynamicLibHandle)
  (var libHandle (* void) null)

  (comptime-cond
   ('Unix
    ;; Clear error
    (dlerror)

    ;; RTLD_LAZY: Don't look up symbols the shared library needs until it encounters them
    ;; RTLD_GLOBAL: Allow subsequently loaded libraries to resolve from this library (mainly for
    ;; compile-time function execution)
    ;; Note that this requires linking with -Wl,-rpath,. in order to turn up relative path .so files
    (set libHandle (dlopen libraryPath (bit-or RTLD_LAZY RTLD_GLOBAL)))

    (var error (* (const char)) (dlerror))
    (when (or (not libHandle) error)
      (fprintf stderr "DynamicLoader Error:\n%s\n" error)
      (return null)))
   ('Windows
    ;; TODO Clean this up! Only the cakelispBin is necessary I think (need to double check that)
    ;; TODO Clear added dirs after? (RemoveDllDirectory())
    (var absoluteLibPath (* (const char))
         (makeAbsolutePath_Allocated null libraryPath))
    (var convertedPath ([] MAX_PATH_LENGTH char) (array 0))
    ;; TODO Remove, redundant with makeAbsolutePath_Allocated()
    (makeBackslashFilename convertedPath (sizeof convertedPath) absoluteLibPath)
    (var dllDirectory ([] MAX_PATH_LENGTH char) (array 0))
    (getDirectoryFromPath convertedPath dllDirectory (sizeof dllDirectory))
    (scope ;; DLL directory
     (var wchars_num int (MultiByteToWideChar CP_UTF8 0 dllDirectory -1 null 0))
     (var wstrDllDirectory (* wchar_t) (new-array wchars_num wchar_t))
     (MultiByteToWideChar CP_UTF8 0 dllDirectory -1 wstrDllDirectory wchars_num)
     (AddDllDirectory wstrDllDirectory)
     (delete-array wstrDllDirectory))

    ;; When loading cakelisp.lib, it will actually need to find cakelisp.exe for the symbols
    ;; This is only necessary for Cakelisp itself; left here for reference
    ;; (scope ;; Cakelisp directory
    ;;  (var cakelispBinDirectory (* (const char))
    ;;    (makeAbsolutePath_Allocated null "bin"))
    ;;  (var wchars_num int (MultiByteToWideChar CP_UTF8 0 cakelispBinDirectory -1 null 0))
    ;;  (var wstrDllDirectory (* wchar_t) (new-array wchars_num wchar_t))
    ;;  (MultiByteToWideChar CP_UTF8 0 cakelispBinDirectory -1 wstrDllDirectory wchars_num)
    ;;  (AddDllDirectory wstrDllDirectory)
    ;;  (free (type-cast cakelispBinDirectory (* void)))
    ;;  (delete-array wstrDllDirectory))

    (set libHandle (LoadLibraryEx convertedPath null
                                  (bit-or LOAD_LIBRARY_SEARCH_USER_DIRS
                                          LOAD_LIBRARY_SEARCH_DEFAULT_DIRS)))
    (unless libHandle
      (fprintf stderr "DynamicLoader Error: Failed to load %s with code %d\n" convertedPath
               (GetLastError))
      (free (type-cast absoluteLibPath (* void)))
      (return null))
    (free (type-cast absoluteLibPath (* void)))))

  (set (at libraryPath dynamicLibraries) (array libHandle))
  (return libHandle))

(defun getSymbolFromDynamicLibrary (library DynamicLibHandle
                                    symbolName (* (const char))
                                    &return (* void))
  (unless library
    (fprintf stderr "DynamicLoader Error: Received empty library handle\n")
    (return null))

  (comptime-cond
   ('Unix
    ;; Clear any existing error before running dlsym
    (var error (* char) (dlerror))
    (when error
      (fprintf stderr "DynamicLoader Error:\n%s\n" error)
      (return null))

    (var symbol (* void) (dlsym library symbolName))

    (set error (dlerror))
    (when error
      (fprintf stderr "DynamicLoader Error:\n%s\n" error)
      (return null))

    (return symbol))
   ('Windows
    (var procedure (* void)
         (type-cast
          (GetProcAddress (type-cast library HINSTANCE) symbolName) (* void)))
    (unless procedure
      (fprintf stderr "DynamicLoader Error:\n%d\n" (GetLastError))
      (return null))
    (return procedure))
   (true
    (return null))))

(defun closeAllDynamicLibraries ()
  (for-in libraryPair (& (<> std::pair (const std::string) DynamicLibrary)) dynamicLibraries
          (comptime-cond
           ('Unix
            (dlclose (field libraryPair second handle)))
           ('Windows
            (FreeLibrary (type-cast (field libraryPair second handle) HMODULE)))))
  (on-call dynamicLibraries clear))

(defun closeDynamicLibrary (handleToClose DynamicLibHandle)
  (var libHandle DynamicLibHandle null)
  (var libraryIt (in DynamicLibraryMap iterator) (on-call dynamicLibraries begin))
  (while (!= libraryIt (on-call dynamicLibraries end))
    (when (= handleToClose (path libraryIt > second . handle))
      (set libHandle (path libraryIt > second . handle))
      (on-call dynamicLibraries erase libraryIt)
      (break))
    (incr libraryIt))

  (unless libHandle
    (fprintf stderr "warning: closing library which wasn't in the list of loaded libraries\n")
    (set libHandle handleToClose))

  (comptime-cond
   ('Unix
    (dlclose libHandle))
   ('Windows
    (FreeLibrary (type-cast libHandle HMODULE)))))
