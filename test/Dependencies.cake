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

(defgenerator unused ()
  (var startToken (& (const Token)) (at startTokenIndex tokens))
  (var startToken (at startTokenIndex tokens) (& (const Token)))
  (var startToken (& (const Token)))
  (var is-good false bool)
  (var is-good bool false)
  (var is-good (neq 1 2) bool)
  (var name "Buster" (* (const char)))
  (var name (* (const char)) "Buster")
  (var name "Buster" std::string)

  (var end-invocation-index (FindCloseParenIndex tokens startTokenIndex) int)

  (var name-index int (getExpectedArgument "expected struct name" tokens startTokenIndex 1 end-invocation-index))

  (var is-global (and (= context.scope EvaluatorScope_Module)
                      (= 0 (call-on
                            (path (at tokens (+ startTokenIndex 1)) contents) compare "defstruct")))
       bool)
  (var output-dest (if isGlobal output.header output.source) (& (<> std::vector StringOutput))))
  (var output-dest (& (<> std::vector StringOutput)) (if isGlobal output.header output.source))

(defstruct-local test-struct
  a ([] 5 int)
  b (* (const char))
  c bool
  tokens (& (<> std::vector Token)))

(defstruct object-reference
  tokens (* (const (<> std::vector Token)))
  start-index int
  context EvaluatorContext
  splice-output (* GeneratorOutput)
  is-resolved bool)
