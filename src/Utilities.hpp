#pragma once

#include <algorithm>  // std::find
#include <cstdio>     //  sprintf
#include <string>
#include <string.h>

#include "Exporting.hpp"

#if defined(UNIX) || defined(MACOS)
#include <strings.h>
#endif

#define MAX_NAME_LENGTH 256
#define MAX_PATH_LENGTH 1024

#ifdef WINDOWS
#define FORMAT_SIZE_T "%zu"
#else
#define FORMAT_SIZE_T "%lu"
#endif

#define ArraySize(array) sizeof((array)) / sizeof((array)[0])

#define FindInContainer(container, element) std::find(container.begin(), container.end(), element)

void printIndentToDepth(int depth);

// Print to stderr. Could be for reporting errors too; it's up to you to add "error:'
#define Logf(format, ...) fprintf(stderr, format, __VA_ARGS__)
#define Log(format) fprintf(stderr, format)

// TODO: de-macroize
#define SafeSnprintf(buffer, size, format, ...)                        \
	{                                                                  \
		int _numPrinted = snprintf(buffer, size, format, __VA_ARGS__); \
		buffer[_numPrinted] = '\0';                                    \
	}

#define PrintfBuffer(buffer, format, ...) SafeSnprintf(buffer, sizeof(buffer), format, __VA_ARGS__)

// TODO Replace with strcat()?
#define PrintBuffer(buffer, output) SafeSnprintf(buffer, sizeof(buffer), "%s", output)

bool writeCharToBuffer(char c, char** at, char* bufferStart, int bufferSize);
bool writeStringToBuffer(const char* str, char** at, char* bufferStart, int bufferSize);

#ifdef WINDOWS
#define StrCatSafe(bufferOut, bufferSize, strToAppend) strcat_s(bufferOut, bufferSize, strToAppend)
#define StrDuplicate(str) _strdup(str)

#ifdef MINGW
#include <string.h>
#define StrCompareIgnoreCase(strA, strB) stricmp(strA, strB)
#else
#define StrCompareIgnoreCase(strA, strB) _stricmp(strA, strB)
#endif

#else
// TODO: Safe version
#define StrCatSafe(bufferOut, bufferSize, strToAppend) \
	strncat(bufferOut, strToAppend, bufferSize - 1)
#define StrDuplicate(str) strdup(str)
#define StrCompareIgnoreCase(strA, strB) strcasecmp(strA, strB)
#endif

// TODO De-macroize these? It could be useful to keep as macros if I add __LINE__ etc. (to answer
// questions like "where is this error coming from?")

// The first character is at 1 (at least, in Emacs, when following this error, it takes you
// to the start of the line with e.g. column 1)
// TODO: Add Clang-style error arrow note via function "print line N of filename"
#define ErrorAtTokenf(token, format, ...)                                                \
	fprintf(stderr, "%s:%d:%d: error: " format "\n", (token).source, (token).lineNumber, \
	        1 + (token).columnStart, __VA_ARGS__)

#define ErrorAtToken(token, message)                                             \
	fprintf(stderr, "%s:%d:%d: error: %s\n", (token).source, (token).lineNumber, \
	        1 + (token).columnStart, message)

#define NoteAtToken(token, message)                                             \
	fprintf(stderr, "%s:%d:%d: note: %s\n", (token).source, (token).lineNumber, \
	        1 + (token).columnStart, message)

#define NoteAtTokenf(token, format, ...)                                                \
	fprintf(stderr, "%s:%d:%d: note: " format "\n", (token).source, (token).lineNumber, \
	        1 + (token).columnStart, __VA_ARGS__)

#define PushBackAll(dest, src) (dest).insert((dest).end(), (src).begin(), (src).end())

FILE* fileOpen(const char* filename, const char* mode);

CAKELISP_API void crc32(const void* data, size_t n_bytes, uint32_t* crc);

// Let this serve as more of a TODO to get rid of std::string
extern std::string EmptyString;
