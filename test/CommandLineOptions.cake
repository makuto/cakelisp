;; This serves as a possible way to handle arguments. It won't actually generate

(defenum language-policy
  only-c
  assume-c++
  mixed-c-c++)

(defenum cakelisp-operation
  build
  clean
  ;; Go from generated source to Cakelisp file/line number, and vise versa
  map)

(defenum name-policy
  pascal-case-if-plural
  camel-case
  underscores)

(defstruct cakelisp-build-options
  files :required :help "The .cake files to build. These files' imports will automatically be added"

  policy language-policy
  name-translation-format name-policy
  :help "How to format symbol names when converting lisp-style-names to c_style_names (example is underscores mode)"

  working-directory (* (const char)) :help "Where to keep cached artifacts for partial builds"
  include-directories (* (* (const char))) :help "Where to search for .cake files"

  ;; Compile-time options
  compile-executable (* (const char)) :help "Path to a C++ compiler (for compile time code)"
  compile-options (* (* (const char))) :help "Use {file} to denote the .cpp file being compiled (for compile time code)"
  link-executable (* (const char))
  :help "Path to an executable capable of linking object (.o) files into dynamic libraries (for compile time code)"
  link-options (* (* (const char))) :help "Use {file} to denote the .o file being linked (for compile time code)"

  ;; Debugging
  verbose-files bool
  verbose-build bool
  verbose-references bool)

(defstruct cakelisp-map-options
  "Find the associated source tokens of the given generated file character position, or vice versa"
  filename (* (const char)) :required
  line int :help "Required if char-position is not provided"
  column int :help "Required if char-position is not provided"
  char-position int
  :help "The index into the array of all characters in the file. This matches e.g. Emacs' goto-char. Starts at 1")
