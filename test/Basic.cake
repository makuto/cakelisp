;; Quite possibly the simplest generator: slap "#include {arg}" into the source file
;; One of these can output multiple includes
(c-import "<stdio.h>")

;; The defun generator will implicitly invoke a (generate-args) generator on the args list. "int"
;;  isn't a generator or a function, it's a DSL symbol generate-args understands
(defun main (int argCount ([] (* char)) argArray &return int)
  (printf "This is a test. Here's a number: %d" 4)
  ;; TODO: Do I want to make (return) a thing, or go with lisp-style last eval returns?
  (return 0))

(defun helper (&return std::string)
  (return "test"))

;; Terminology notes: :thing is a keyword symbol. &thing is a symbol or marker symbol (maybe I call it a sentinel?)
