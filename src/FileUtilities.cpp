#include "FileUtilities.hpp"

#include <stdio.h>

#ifdef UNIX
#include <sys/stat.h>
#else
#error Need to implement file utilities for this platform
#endif

bool fileIsMoreRecentlyModified(const char* filename, const char* reference)
{
#ifdef UNIX
	struct stat fileStat;
	struct stat referenceStat;
	if (stat(filename, &fileStat) == -1)
	{
		perror("fileIsMoreRecentlyModified: ");
		return true;
	}
	if (stat(reference, &referenceStat) == -1)
	{
		perror("fileIsMoreRecentlyModified: ");
		return true;
	}

	return fileStat.st_mtime > referenceStat.st_mtime;
#else
	// Err on the side of filename always being newer than the reference. The #error in the includes
	// block should prevent this from ever being compiled anyways
	return true;
#endif
}

void makeDirectory(const char* path)
{
	#ifdef UNIX
	if (mkdir(path, 0755) == -1)
		perror("makeDirectory: ");
	#else
	#error Need to be able to make directories on this platform
	#endif
}
