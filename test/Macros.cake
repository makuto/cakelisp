(defmacro destructure-arguments ()
  (var end-invocation-index int (FindCloseParenTokenIndex tokens startTokenIndex))
  ;; This needs to be inserted for the caller
  ;; (when (not (ExpectNumArguments tokens startTokenIndex end-invocation-index (token-splice))
    ;; (return false))
  ;; Find the end invocation for the caller, not us
  (tokenize-push output
                 (var destr-end-invocation-index int
                        (FindCloseParenTokenIndex tokens startTokenIndex)))
  (var start-args-index int (+ 2 startTokenIndex))
  (var current-arg-index int start-args-index)
  (var num-destructured-args int 0)
  (while (< current-arg-index end-invocation-index)
    (var current-arg (* (const Token)) (addr (at current-arg-index tokens)))
    (var num-destructured-args-token Token (array TokenType_Symbol (std::to_string num-destructured-args)
                                                  "test/Macros.cake" 1 1 1))
    ;; TODO: This should add expect argument stuff for error checking
    (tokenize-push output
                   (var (token-splice current-arg) int
                        (+ startTokenIndex 2 (token-splice (addr num-destructured-args-token)))))
    (++ num-destructured-args)
    (set current-arg-index
         (getNextArgument tokens current-arg-index end-invocation-index)))
  (printTokens output)
  (return true))

;; Assumes tokens is the array of tokens
(defmacro quick-token-at ()
  (destructure-arguments name index)
  (tokenize-push output (var (token-splice (addr (at name tokens))) (& (const Token))
                             (at (token-splice (addr (at index tokens))) tokens)))
  (return true))

(defmacro def-serialize-struct ()
  (var end-invocation-index int (FindCloseParenTokenIndex tokens startTokenIndex))
  (destructure-arguments name-index first-member-index)
  (quick-token-at struct-name name-index)

  (var member-tokens (<> std::vector Token))
  (var serialize-tokens (<> std::vector Token))
  (var current-member int first-member-index)
  (while (< current-member end-invocation-index)
    (var member-name-token (& (const Token)) (at current-member tokens))
    (var member-type-token (& (const Token)) (at (+ 1 current-member) tokens))
    (on-call member-tokens push_back member-name-token)
    (on-call member-tokens push_back member-type-token)
    (when (= 0 (on-call (field member-type-token contents) compare "int"))
      (tokenize-push serialize-tokens
                     (load-int (addr (field out (token-splice (addr member-name-token)))))))
    (when (= 0 (on-call (field member-type-token contents) compare "bool"))
      (tokenize-push serialize-tokens
                     (load-bool (addr (field out (token-splice (addr member-name-token)))))))
    ;; Advance once for name and second time for type
    (set current-member (getNextArgument tokens current-member end-invocation-index))
    (set current-member (getNextArgument tokens current-member end-invocation-index)))

  (printTokens member-tokens)

  ;; Output the struct definition
  (tokenize-push output
                 (defstruct (token-splice (addr struct-name))
                   (token-splice-array member-tokens)))

  ;; Make the function which will load our struct!
  ;; In a real serializer, I would want the following features:
  ;; - Serialize to plain text. This is useful both for runtime printing and for saving data to version control
  ;; - Serialize entire struct to binary, when possible. If all members are plain old data, read/write
  ;;   the bytes in one shot
  ;; - Be smart about serializing things like enums, which should include text/CRC of the enum name
  ;; - For use-cases where the data needs to be discarded on struct change, store a CRC of the struct definition
  ;; - Callbacks for special types (e.g. fill in external pointer), custom serializers, etc.
  (var func-name ([] 256 char) (array 0))
  (PrintfBuffer func-name "%s-load" (on-call (field struct-name contents) c_str))
  (var struct-serialize-func-name Token (array TokenType_Symbol func-name
                                               (field struct-name source)
                                               (field struct-name lineNumber)
                                               (field struct-name columnStart)
                                               (field struct-name columnEnd)))
  (when (not (on-call serialize-tokens empty))
      (tokenize-push output
                     (defun (token-splice (addr struct-serialize-func-name))
                         (out (* (token-splice (addr struct-name)))
                          file (* FILE))
                   (token-splice-array serialize-tokens))))
  (return true))

(def-serialize-struct serialize-me
    a int
    b bool)
