(import "TextAdventure.cake")

(c-import "<stdio.h>")

(defun test ()
  (printf "%lu array\n" (sizeof rooms)))
