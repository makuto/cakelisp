;; Quite possibly the simplest generator: slap "#include {arg}" into the source file
;; One of these can output multiple includes
(c-import "<stdio.h>" &with-decls "<vector>")

;; The defun generator will implicitly invoke a (generate-args) generator on the args list. "int"
;;  isn't a generator or a function, it's a DSL symbol generate-args understands
(defun main (int arg-count ([] (* char)) args &return int)
  (printf "This is a test. Here's a number: %d" 4)
  ;; TODO: Do I want to make (return) a thing, or go with lisp-style last eval returns?
  (return 0))

(defun helper (&return std::string)
  (return "test"))

(defun test-complex-args ((* (const char)) filename
                          ;; Need to upcase singular argument type names manually (see PascalCaseIfPlural comments)
                          (& (const (<> std::vector Token))) tokens
                          int start-token-index
                          (& (<> std::vector (* generate-operation) other-thing)) operations-out)
  (return 0))

(defun test-array-args (([] int) numbers ([] ([] 5 char)) five-letter-words
                        ;; ([] 4 ([] 4 float)) matrix ;; C doesn't like this, but the Cakelisp type system can handle it
                        &return (* char))
  (return (nth 0 five-letter-words)))

;; Terminology notes: :thing is a keyword symbol. &thing is a symbol or marker symbol (maybe I call it a sentinel?)

(defun + (int a int b &return int)
  (return 0))

(defun vec-+ (int a int b &return int)
  (return 0))
