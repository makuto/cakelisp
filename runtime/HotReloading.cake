(set-cakelisp-option use-c-linkage true)

(import "DynamicLoader.cake"
        &comptime-only "Macros.cake")
(c-import "<unordered_map>" "<vector>")

(comptime-cond
 ('Unix
  (c-import "<sys/stat.h>" "<unistd.h>")))

;;
;; Function management
;;

(def-type-alias FunctionReferenceArray (<> std::vector (* (* void))))
(def-type-alias FunctionReferenceMap (<> std::unordered_map std::string FunctionReferenceArray))
(def-type-alias FunctionReferenceMapIterator (in FunctionReferenceMap iterator))
(def-type-alias FunctionReferenceMapPair (<> std::pair (const std::string) FunctionReferenceArray))

(var registered-functions FunctionReferenceMap)

(var current-lib DynamicLibHandle null)
(comptime-cond
 ('Unix
  (var hot-reload-lib-path (* (const char)) "libGeneratedCakelisp.so")
  ;; Not used on Unix due to more advanced versioning required to avoid caching issues
  (var hot-reload-active-lib-path (* (const char)) "libGeneratedCakelisp_Active.so"))
 ('Windows
  (var hot-reload-lib-path (* (const char)) "libGeneratedCakelisp.dll")
  ;; Need to copy it so the programmer can modify the other file while we're running this one
  (var hot-reload-active-lib-path (* (const char)) "libGeneratedCakelisp_Active.dll")))

;; This is incremented each time a reload occurs, to avoid caching problems on Linux
(var hot-reload-lib-version-number int 0)

(defun register-function-pointer (function-pointer (* (* void))
                                  function-name (* (const char)))
  (var findIt FunctionReferenceMapIterator
       (call-on find registered-functions function-name))
  (if (= findIt (call-on end registered-functions))
      (block
          (var new-function-pointer-array FunctionReferenceArray)
        (call-on push_back new-function-pointer-array function-pointer)
        (set (at function-name registered-functions)
             ;; This could also be written as (std::move new-function-pointer-array)
             ;; I'm not sure how I want it to work
             (call (in std move) new-function-pointer-array)))
      (call-on push_back (path findIt > second) function-pointer)))

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
    (dynamic-library-close current-lib))

  ;; Load the library. This requires extra effort for two reasons:
  ;;  1) We can't update the library if it's currently loaded, so we must run on a copy
  ;;  2) Linux has caching mechanisms which force us to create uniquely-named copies
  (comptime-cond
   ('Unix
    ;; We need to use a new temporary file each time we do a reload, otherwise mmap/cache issues segfault
    ;; See https://bugzilla.redhat.com/show_bug.cgi?id=1327623
    (scope ;; Clean up the old version, so they don't stack up and eat all the disk space
     (var prev-library-name ([] 256 char) (array 0))
     (snprintf prev-library-name (sizeof prev-library-name) "libHotReloadingTemp_%d.so"
               hot-reload-lib-version-number)
     (when (!= -1 (access prev-library-name F_OK))
       (remove prev-library-name)))

    ;; Must be a unique filename, else the caching will bite us
    (incr hot-reload-lib-version-number)
    (var temp-library-name ([] 256 char) (array 0))
    (snprintf temp-library-name (sizeof temp-library-name) "libHotReloadingTemp_%d.so"
              hot-reload-lib-version-number)
    (unless (copy-binary-file-to hot-reload-lib-path temp-library-name)
      (return false))
    ;; False = don't use global scope, otherwise symbols may bind to the old version!
    (set current-lib (dynamic-library-load temp-library-name false)))

   (true ;; Other platforms don't need the fancy versioning
    (unless (copy-binary-file-to hot-reload-lib-path hot-reload-active-lib-path)
      (return false))
    ;; False = don't use global scope, otherwise symbols may bind to the old version!
    (set current-lib (dynamic-library-load hot-reload-active-lib-path false))))

  (unless current-lib
    (return false))

  ;; Intialize variables
  (var global-initializer (* void)
       (dynamic-library-get-symbol current-lib "hotReloadInitializeState"))
  (if global-initializer
      (block
          (def-function-signature global-initializer-signature ())
        (call (type-cast global-initializer global-initializer-signature)))
      (printf "warning: global initializer 'hotReloadInitializeState' not found!"))

  (for-in function-referent-it (& FunctionReferenceMapPair) registered-functions
          (var loaded-symbol (* void)
               (dynamic-library-get-symbol current-lib
                                           (call-on c_str (path function-referent-it . first))))
          (unless loaded-symbol
            (return false))
          ;; TODO: What will happen once modules are unloaded? We can't store pointers to their static memory
          (for-in function-pointer (* (* void)) (path function-referent-it . second)
                  (set (deref function-pointer) loaded-symbol)))
  (return true))

(defun hot-reload-clean-up ()
  (dynamic-library-close-all))

;;
;; Data/state management
;;

(def-type-alias StateVariableMap (<> std::unordered_map std::string (* void)))
(def-type-alias StateVariableMapIterator (in StateVariableMap iterator))

(var registered-state-variables StateVariableMap)
(var verbose-variables bool false)

(defun hot-reload-find-variable (name (* (const char)) variable-address-out (* (* void)) &return bool)
  (var find-it StateVariableMapIterator (call-on find registered-state-variables name))
  (unless (!= find-it (call-on end registered-state-variables))
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

(comptime-cond
 ;; Did this weird thing because comptime-cond doesn't have (not)
 ('No-Hot-Reload-Options) ;; Make sure to not touch environment (they only want headers)
 (true
  (comptime-cond
   ('Unix
    ;; Search the current directory for hot-reloading library to load
    (add-library-runtime-search-directory ".")
    ;; Make sure the thing which gets loaded can access our API
    (add-linker-options "--export-dynamic")
    (add-build-options "-fPIC")))))
