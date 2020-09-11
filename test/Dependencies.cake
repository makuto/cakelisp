(include "<stdio.h>")
;; TODO: Add relative path support
(include "test/DependenciesModule.cake")

(defun main (&return int)
  (printf "%s\n" (hello-from-comptime))
  (empty-macro)
  (test-generator)
  (return 0))

(defmacro hello-from-comptime ()
  (var (& (const Token)) startToken (at startTokenIndex tokens))
  (output.push_back (array TokenType_String "Hello, world!"
                           startToken.source startToken.lineNumber
                           startToken.columnStart startToken.columnEnd))
  (return true))

(defmacro amazing-print-create ()
  (var (& (const Token)) startToken (at startTokenIndex tokens))
  ;; I need quoting soon lol
  (output.push_back (array TokenType_OpenParen ""
                           startToken.source startToken.lineNumber
                           startToken.columnStart startToken.columnEnd))
  (output.push_back (array TokenType_Symbol "defun"
                           startToken.source startToken.lineNumber
                           startToken.columnStart startToken.columnEnd))
  (output.push_back (array TokenType_Symbol "amazing-print"
                           startToken.source startToken.lineNumber
                           startToken.columnStart startToken.columnEnd))
  (output.push_back (array TokenType_OpenParen ""
                           startToken.source startToken.lineNumber
                           startToken.columnStart startToken.columnEnd))
  (output.push_back (array TokenType_CloseParen ""
                           startToken.source startToken.lineNumber
                           startToken.columnStart startToken.columnEnd))
  (output.push_back (array TokenType_OpenParen ""
                           startToken.source startToken.lineNumber
                           startToken.columnStart startToken.columnEnd))
  (output.push_back (array TokenType_Symbol "printf"
                           startToken.source startToken.lineNumber
                           startToken.columnStart startToken.columnEnd))
  (output.push_back (array TokenType_String "It worked"
                           startToken.source startToken.lineNumber
                           startToken.columnStart startToken.columnEnd))
  (output.push_back (array TokenType_CloseParen ""
                           startToken.source startToken.lineNumber
                           startToken.columnStart startToken.columnEnd))
  (output.push_back (array TokenType_CloseParen ""
                           startToken.source startToken.lineNumber
                           startToken.columnStart startToken.columnEnd))
  ;; (bad-function)
  (return true))

(amazing-print-create)

(defgenerator test-generator ()
  (var (& (const Token)) startToken (at startTokenIndex tokens))
  (addStringOutput output.source "printf(\"This is a test\");"
                   StringOutMod_NewlineAfter (addr startToken))
  (return true))
