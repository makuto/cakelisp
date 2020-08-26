
;; Hmm, how should this handle other types? With macros!
(defgenerator + (int a int b &rest int more)
  (let ((c-var-name (make-c-var-name)) (c-type "int"))
  (c-out "%s %s = %s + %s;" c-type c-var-name `a `b)
  (for additional more
       (c-out "%s += %s;" c-var-name `additional))))

(defmacro make-+-for-type (type)
  ;; TODO: Figure out quoting. The quote before the ( is probably bad for tokenizer
  `(defgenerator + (,type a ,type b &rest ,type more)
     (let ((c-var-name (make-c-var-name)) (c-type ,type))
       (c-out "%s %s = %s + %s;" c-type c-var-name `a `b)
       (for additional more
            (c-out "%s += %s;" c-var-name `additional)))))