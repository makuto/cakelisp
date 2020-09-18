#pragma once

// Returns true if the reference file doesn't exist. This is under the assumption that this function
// is always used to check whether it is necessary to e.g. build something if the source is newer
// than the previous build (or the source has never been built)
bool fileIsMoreRecentlyModified(const char* filename, const char* reference);

void makeDirectory(const char* path);

void getDirectoryFromPath(const char* path, char* bufferOut, int bufferSize);
void getFilenameFromPath(const char* path, char* bufferOut, int bufferSize);
