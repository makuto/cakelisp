#pragma once

#include <cstdio>  //  sprintf

void printIndentToDepth(int depth);

#define SafeSnprinf(buffer, size, format, ...)                         \
	{                                                                  \
		int _numPrinted = snprintf(buffer, size, format, __VA_ARGS__); \
		buffer[_numPrinted] = '\0';                                    \
	}

#define PrintfBuffer(buffer, format, ...) SafeSnprinf(buffer, sizeof(buffer), format, __VA_ARGS__)

// TODO Replace with strcat()
#define PrintBuffer(buffer, output) SafeSnprinf(buffer, sizeof(buffer), "%s", output)

// The first character is at 1 (at least, in Emacs, when following this error, it takes you
// to the start of the line with e.g. column 1)
// TODO: Add Clang-style error arrow note via function "print line N of filename"
#define ErrorAtTokenf(filename, token, format, ...)						\
	printf("%s:%d:%d: error: " format "\n", filename, token.lineNumber, 1 + token.columnStart, \
	       __VA_ARGS__)

#define ErrorAtToken(filename, token, message) \
	printf("%s:%d:%d: error: %s\n", filename, (token).lineNumber, 1 + (token).columnStart, message)
