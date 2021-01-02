#pragma once

extern const char* compilerObjectExtension;
extern const char* linkerDynamicLibraryPrefix;
extern const char* linkerDynamicLibraryExtension;
extern const char* defaultExecutableName;

void makeIncludeArgument(char* buffer, int bufferSize, const char* searchDir);

// On Windows, extra formatting is required to output objects
void makeObjectOutputArgument(char* buffer, int bufferSize, const char* objectName);
void makeDynamicLibraryOutputArgument(char* buffer, int bufferSize, const char* libraryName,
                                      const char* buildExecutable);

// On Windows, extra work is done to find the compiler and linker executables. This function handles
// looking up those environment variables to determine which executable to use. On Linux, this
// function might be a good place for executable search paths, e.g. check /usr/bin for g++
bool resolveExecutablePath(const char* fileToExecute, char* resolvedPathOut,
                           int resolvedPathOutSize);
