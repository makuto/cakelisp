#pragma once

#include "FileTypes.hpp"

// Returns zero if the file doesn't exist, or there was some other error
FileModifyTime fileGetLastModificationTime(const char* filename);

// Returns true if the reference file doesn't exist. This is under the assumption that this function
// is always used to check whether it is necessary to e.g. build something if the source is newer
// than the previous build (or the source has never been built)
bool fileIsMoreRecentlyModified(const char* filename, const char* reference);

bool fileExists(const char* filename);

void makeDirectory(const char* path);

void getDirectoryFromPath(const char* path, char* bufferOut, int bufferSize);
void getFilenameFromPath(const char* path, char* bufferOut, int bufferSize);
// Given e.g. filepath = thing/src/MyCode.cake, referencedFilePath = MyCode.cpp, sets bufferOut to
// "thing/src/MyCode.cpp"
void makePathRelativeToFile(const char* filePath, const char* referencedFilePath, char* bufferOut,
                            int bufferSize);
// Returns null if the file does not exist
// Use free() on the returned value if non-null
const char* makeAbsolutePath_Allocated(const char* fromDirectory, const char* filePath);
// Will be absolute if above working dir, else, normalized relative
void makeAbsoluteOrRelativeToWorkingDir(const char* filePath, char* bufferOut, int bufferSize);

// Turns a/b/file.txt into outputDir/file.txt.addExtension
bool outputFilenameFromSourceFilename(const char* outputDir, const char* sourceFilename,
                                      const char* addExtension, char* bufferOut, int bufferSize);

bool copyBinaryFileTo(const char* srcFilename, const char* destFilename);
bool copyFileTo(const char* srcFilename, const char* destFilename);

// Non-binary files only
bool moveFile(const char* srcFilename, const char* destFilename);

void addExecutablePermission(const char* filename);

// Some Windows APIs require backslashes
void makeBackslashFilename(char* buffer, int bufferSize, const char* filename);

// Does NOT validate whether your buffer can fit the new extension + null terminator
// TODO: Safer version
bool changeExtension(char* buffer, const char* newExtension);
