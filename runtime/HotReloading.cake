(import "DynamicLoader.cake"
        &comptime-only "Macros.cake")
(c-import "<unordered_map>" "<vector>")

(comptime-cond
 ('Unix
  (c-import "<sys/stat.h>")))

;;
;; Function management
;;

(def-type-alias FunctionReferenceArray (<> std::vector (* (* void))))
(def-type-alias FunctionReferenceMap (<> std::unordered_map std::string FunctionReferenceArray))
(def-type-alias FunctionReferenceMapIterator (in FunctionReferenceMap iterator))
(def-type-alias FunctionReferenceMapPair (<> std::pair (const std::string) FunctionReferenceArray))

(var registered-functions FunctionReferenceMap)

(var current-lib DynamicLibHandle null)

(var hot-reload-lib-path (* (const char)) "libGeneratedCakelisp.so")
;; Need to copy it so the programmer can modify the other file while we're running this one
(var hot-reload-active-lib-path (* (const char)) "libGeneratedCakelisp_Active.so")

;; This is incremented each time a reload occurs, to avoid caching problems on Linux
(var hot-reload-lib-version-number int 0)

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

(defun-local copy-binary-file-to (srcFilename (* (const char))
                                  destFilename (* (const char)) &return bool)
	;; Note: man 3 fopen says "b" is unnecessary on Linux, but I'll keep it anyways
	(var srcFile (* FILE) (fopen srcFilename "rb"))
	(var destFile (* FILE) (fopen destFilename "wb"))
	(when (or (not srcFile) (not destFile))
		(perror "fopen: ")
		(fprintf stderr "error: failed to copy %s to %s\n" srcFilename destFilename)
		(return false))

	(var buffer ([] 4096 char))
	(var totalCopied size_t 0)
	(var numRead size_t (fread buffer (sizeof (at 0 buffer)) (array-size buffer) srcFile))
	(while numRead
		(fwrite buffer (sizeof (at 0 buffer)) numRead destFile)
		(set totalCopied (+ totalCopied numRead))
		(set numRead (fread buffer (sizeof (at 0 buffer)) (array-size buffer) srcFile)))

	(fclose srcFile)
	(fclose destFile)

	(return true))

(defun do-hot-reload (&return bool)
  (when current-lib
    (closeDynamicLibrary current-lib))

  ;; Load the library. This requires extra effort for two reasons:
  ;;  1) We can't update the library if it's currently loaded, so we must run on a copy
  ;;  2) Linux has caching mechanisms which force us to create uniquely-named copies
  (comptime-cond
   ('Unix
    ;; Make active library executable. May not be necessary!
    ;; (unless (= 0 (chmod hot-reload-active-lib-path
                        ;; (bit-or S_IRUSR S_IWUSR S_IXUSR S_IRGRP S_IXGRP S_IROTH S_IXOTH)))
	  ;; (perror "Making active library executable error: ")
    ;; (return false))))

    ;; We need to use a new temporary file each time we do a reload, otherwise mmap/cache issues segfault
    ;; See https://bugzilla.redhat.com/show_bug.cgi?id=1327623

    ;; TODO: Check for exists before delete (or, check version number)
    (scope ;; Clean up the old version, so they don't stack up and eat all the disk space
     (var prev-library-name ([] 256 char) (array 0))
     (snprintf prev-library-name (sizeof prev-library-name) "libHotReloadingTemp_%d.so"
               hot-reload-lib-version-number)
     ;; TODO Enable
     ;;(remove prev-library-name)
     )

    ;; Must be a unique filename, else the caching will bite us
    (incr hot-reload-lib-version-number)
    (var temp-library-name ([] 256 char) (array 0))
    (snprintf temp-library-name (sizeof temp-library-name) "libHotReloadingTemp_%d.so"
              hot-reload-lib-version-number)
    (unless (copy-binary-file-to hot-reload-lib-path temp-library-name)
      (return false))
    (set current-lib (loadDynamicLibrary temp-library-name)))

   (true ;; Other platforms don't need the fancy versioning
    (unless (copy-binary-file-to hot-reload-lib-path hot-reload-active-lib-path)
      (return false)))
   (set current-lib (loadDynamicLibrary hot-reload-active-lib-path)))

  (unless current-lib
    (return false))

  ;; Intialize variables
  (var global-initializer (* void)
       (getSymbolFromDynamicLibrary current-lib "hotReloadInitializeState"))
  (if global-initializer
      (block
          (def-function-signature global-initializer-signature ())
        (call (type-cast global-initializer global-initializer-signature))
        (printf "Called hotReloadInitializeState\n"))
      (printf "warning: global initializer 'hotReloadInitializeState' not found!"))

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
;; Data/state management
;;

(def-type-alias StateVariableMap (<> std::unordered_map std::string (* void)))
(def-type-alias StateVariableMapIterator (in StateVariableMap iterator))

(var registered-state-variables StateVariableMap)
(var verbose-variables bool true)

(defun hot-reload-find-variable (name (* (const char)) variable-address-out (* (* void)) &return bool)
  (var find-it StateVariableMapIterator (on-call registered-state-variables find name))
  (unless (!= find-it (on-call registered-state-variables end))
    (set variable-address-out nullptr)
    (when verbose-variables
      (printf "Did not find variable %s\n" name))
    (return false))
  (when verbose-variables
    (printf "Found variable %s at %p.\n" name (path find-it > second)))
  (set (deref variable-address-out) (path find-it > second))
  (return true))

;; TODO: Free variables. They'll need generated destructors for C++ types (see compile-time vars)
(defun hot-reload-register-variable (name (* (const char)) variable-address (* void))
  (set (at name registered-state-variables) variable-address))

;;
;; Building
;;

;; TODO: This only makes sense on a per-target basis. Instead, modules should be able to append
;; arguments to the link command only
(set-cakelisp-option build-time-linker "/usr/bin/g++")
;; This needs to link -ldl and such (depending on platform...)
(set-cakelisp-option build-time-link-arguments
                     ;; "-shared" ;; This causes c++ initializers to fail and no linker errors. Need to only enable on lib?
                     "-o" 'executable-output 'object-input
                     ;; TODO: OS dependent
                     ;; Need --export-dynamic so the loaded library can use our symbols
                     "-ldl" "-lpthread" "-Wl,-rpath,.,--export-dynamic")

(add-build-options "-fPIC")
