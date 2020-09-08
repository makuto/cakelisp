(include "<stdio.h>")

(defun main (&return int)
  ;; TODO: Need to guess on normal functions, not just compile time functions
  (printf "%s\n" (macro-a))
  (macro-b)
  (return 0))

(defmacro macro-a ()
  (var (& (const Token)) startToken (at startTokenIndex tokens))
  (output.push_back (array TokenType_String "Hello, world!"
                           startToken.source startToken.lineNumber
	                       startToken.columnStart startToken.columnEnd))
  (return true))

(defmacro macro-b ()
  (return true))

;; TODO: This should invoke
(macro-a)
