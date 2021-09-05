(c-import "<stdio.h>")

(defun main (&return int)
  (printf "Hello, execute!\n" \
          "This is a long message, broken " \
          "across multiple lines. " \ ;; Here's a comment on the same line
          "We can also just insert newlines,
 unlike C. That's a bit
 different, isn't it?\n
Note that you need to manually insert your own space, even
though
there
are
newlines. It's a trade-off.\n")
  (printf "%d. How about That?
" 42)
  (printf "\nMy shopping list:\n
    eggs\n
    bacon\n
    sword\n
"
          )
  (printf ###This is a "here-string". All the characters that I type should literally appear,
          including that indentation###)
  (return 0))

(set-cakelisp-option executable-output "test/MultiLineStrings")
