#pragma once

#include <cstdio>  //  sprintf

#define MAX_NAME_LENGTH 128

void printIndentToDepth(int depth);

// TODO: de-macroize
#define SafeSnprinf(buffer, size, format, ...)                         \
	{                                                                  \
		int _numPrinted = snprintf(buffer, size, format, __VA_ARGS__); \
		buffer[_numPrinted] = '\0';                                    \
	}

#define PrintfBuffer(buffer, format, ...) SafeSnprinf(buffer, sizeof(buffer), format, __VA_ARGS__)

// TODO Replace with strcat()
#define PrintBuffer(buffer, output) SafeSnprinf(buffer, sizeof(buffer), "%s", output)

// TODO De-macroize these? It could be useful to keep as macros if I add __LINE__ etc. (to answer
// questions like "where is this error coming from?")

// The first character is at 1 (at least, in Emacs, when following this error, it takes you
// to the start of the line with e.g. column 1)
// TODO: Add Clang-style error arrow note via function "print line N of filename"
#define ErrorAtTokenf(token, format, ...)                                       \
	printf("%s:%d:%d: error: " format "\n", (token).source, (token).lineNumber, \
	       1 + (token).columnStart, __VA_ARGS__)

#define ErrorAtToken(token, message)                                                             \
	printf("%s:%d:%d: error: %s\n", (token).source, (token).lineNumber, 1 + (token).columnStart, \
	       message)
