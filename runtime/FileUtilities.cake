(c-import "<stdio.h>" "<string>" "<assert.h>"
          &with-decls "<stdio.h>") ;; FILE* TODO: How can I remove this from header?

(c-preprocessor-define MAX_PATH_LENGTH 256)

(comptime-cond
 ('Unix
  (c-import "<stdlib.h>" "<libgen.h>" "<string.h>"))
 ('Windows
  (c-preprocessor-define WIN32_LEAN_AND_MEAN)
  (c-import "<windows.h>"))
 (true
  ;; If you're hitting this, you may need to port this over to whatever new platform you are on
  (comptime-error
   "This module requires platform-specific code. Please define your platform before importing" \
   " this module, e.g.: (comptime-define-symbol 'Unix). Supported platforms: 'Unix, 'Windows")))

(defun make-absolute-path-allocated (fromDirectory (* (const char)) filePath (* (const char))
                                     &return (* (const char)))
  (comptime-cond
   ('Unix
	;; Second condition allows for absolute paths
	(if (and fromDirectory (!= (at 0 filePath) '/'))
        (scope
		 (var relativePath ([] MAX_PATH_LENGTH char) (array 0))
		 (safe-string-print relativePath (sizeof relativePath) "%s/%s" fromDirectory filePath)
		 (return (realpath relativePath null)))
        (scope
		 ;; The path will be relative to the binary's working directory
		 (return (realpath filePath null)))))
   ('Windows
	(var absolutePath (* char) (type-cast (calloc MAX_PATH_LENGTH (sizeof char)) (* char)))
	(var isValid bool false)
	(if fromDirectory
        (scope
		 (var relativePath ([] MAX_PATH_LENGTH char) (array 0))
		 (safe-string-print relativePath (sizeof relativePath) "%s/%s" fromDirectory filePath)
		 (set isValid (_fullpath absolutePath relativePath MAX_PATH_LENGTH)))
		(set isValid (_fullpath absolutePath filePath MAX_PATH_LENGTH)))

	(unless isValid
	  (free absolutePath)
	  (return null))
	(return absolutePath))
   (true
    (comptime-error "Need to be able to normalize path on this platform")
	(return null))))

(defun get-directory-from-path (path (* (const char)) bufferOut (* char) bufferSize int)
  (comptime-cond
   ('Unix
	(var pathCopy (* char) (string-duplicate path))
	(var dirName (* (const char)) (dirname pathCopy))
	(safe-string-print bufferOut bufferSize "%s" dirName)
	(free pathCopy))
   ('Windows
	(var drive ([] _MAX_DRIVE char))
	(var dir ([] _MAX_DIR char))
	;; char fname[_MAX_FNAME];
	;; char ext[_MAX_EXT];
	(_splitpath_s path drive (sizeof drive) dir (sizeof dir)
	              null 0 ;; fname
	              null 0) ;; extension
	(_makepath_s bufferOut bufferSize drive dir
                 null ;; fname
	             null)) ;; extension
   (true
    (comptime-error "Need to be able to strip path on this platform"))))

(defun make-backslash-filename (buffer (* char) bufferSize
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

(defmacro safe-string-print (buffer any size any
                             format string &rest args any)
  (tokenize-push output
   (scope
    (var num-printed int (snprintf (token-splice buffer size format)
                                   (token-splice-rest args tokens)))
    (set (at num-printed (token-splice buffer)) '\0')))
 (return true))

(defmacro string-duplicate (string-to-dup any)
  (comptime-cond
   ('Unix
    (tokenize-push output
                   (strdup (token-splice string-to-dup))))
   ('Windows
    (tokenize-push output
                   (_strdup (token-splice string-to-dup)))))
  (return true))

;; TODO: Windows CreateFile version of this
(defun read-file-into-memory (in-file (* FILE) &return (* char))
  (fseek in-file 0 SEEK_END)
  (var file-size size_t (ftell in-file))
  (rewind in-file)
  (var-cast-to out-buffer (* char) (malloc file-size))
  (fread out-buffer file-size 1 in-file)
  (return out-buffer))

(defun write-string (out-file (* FILE) out-string (* (const char)))
  (var string-length size_t (strlen out-string))
  ;; (fprintf stderr "%s has length %d\n" out-string (type-cast string-length int))
  (fwrite (addr string-length) (sizeof string-length) 1 out-file)
  (fwrite out-string string-length 1 out-file))

(defun read-string (in-file (* FILE) out-string-buffer (* char) out-string-buffer-length size_t)
  (var string-length size_t 0)
  (fread (addr string-length) (sizeof string-length) 1 in-file)
  ;; (fprintf stderr "Next string has length %d\n" (type-cast string-length int))
  (assert (<= string-length out-string-buffer-length))
  (fread out-string-buffer string-length 1 in-file))

;; Plain old data with a known size
(defmacro write-pod (item any out-file any)
  (tokenize-push output
    (fwrite (addr (token-splice item)) (sizeof (token-splice item)) 1 (token-splice out-file)))
  (return true))

(defmacro read-pod (item any in-file any)
  (tokenize-push output
    (fread (addr (token-splice item)) (sizeof (token-splice item)) 1 (token-splice in-file)))
  (return true))
