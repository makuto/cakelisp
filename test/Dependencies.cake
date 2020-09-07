(include "<stdio.h>")

(defun main (&return int)
  (macro-a)
  ;; (macro-b)
  (return 0))

(defmacro macro-a ()
  (var (& (const Token)) startToken (at startTokenIndex tokens))
  (output.push_back (array TokenType_Symbol "*"
                           startToken.source startToken.lineNumber
	                       startToken.columnStart startToken.columnEnd))
  (return true))
