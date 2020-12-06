#include "FileUtilities.hpp"

#include <stdio.h>
#include <string.h>

#include "Logging.hpp"
#include "Utilities.hpp"

#ifdef UNIX
#include <errno.h>
#include <fcntl.h>
#include <libgen.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <unistd.h>
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
		if (log.fileSystem || errno != ENOENT)
			perror("fileIsMoreRecentlyModified: ");
		return true;
	}
	if (stat(reference, &referenceStat) == -1)
	{
		if (log.fileSystem || errno != ENOENT)
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

bool fileExists(const char* filename)
{
	return access(filename, F_OK) != -1;
}

void makeDirectory(const char* path)
{
#ifdef UNIX
	if (mkdir(path, 0755) == -1)
	{
		// We don't care about EEXIST, we just want the dir
		if (log.fileSystem || errno != EEXIST)
			perror("makeDirectory: ");
	}
#else
#error Need to be able to make directories on this platform
#endif
}

void getDirectoryFromPath(const char* path, char* bufferOut, int bufferSize)
{
#ifdef UNIX
	char* pathCopy = strdup(path);
	const char* dirName = dirname(pathCopy);
	SafeSnprinf(bufferOut, bufferSize, "%s", dirName);
	free(pathCopy);
#else
#error Need to be able to strip file from path to get directory on this platform
#endif
}

void getFilenameFromPath(const char* path, char* bufferOut, int bufferSize)
{
#ifdef UNIX
	char* pathCopy = strdup(path);
	const char* fileName = basename(pathCopy);
	SafeSnprinf(bufferOut, bufferSize, "%s", fileName);
	free(pathCopy);
#else
#error Need to be able to strip path to get filename on this platform
#endif
}

void makePathRelativeToFile(const char* filePath, const char* referencedFilePath, char* bufferOut,
                            int bufferSize)
{
	getDirectoryFromPath(filePath, bufferOut, bufferSize);
	// TODO: Need to make this safe!
	strcat(bufferOut, "/");
	strcat(bufferOut, referencedFilePath);
}

const char* makeAbsolutePath_Allocated(const char* fromDirectory, const char* filePath)
{
#ifdef UNIX
	// Second condition allows for absolute paths
	if (fromDirectory && filePath[0] != '/')
	{
		char relativePath[MAX_PATH_LENGTH] = {0};
		PrintfBuffer(relativePath, "%s/%s", fromDirectory, filePath);
		return realpath(relativePath, nullptr);
	}
	else
	{
		// The path will be relative to the binary's working directory
		return realpath(filePath, nullptr);
	}
#else
#error Need to be able to normalize path on this platform
#endif
}

void makeAbsoluteOrRelativeToWorkingDir(const char* filePath, char* bufferOut, int bufferSize)
{
#ifdef UNIX
	// If it's already absolute, keep it that way
	// Accept a lone . as well, for current working directory
	if (filePath[0] == '/' || (filePath[0] == '.' && filePath[1] == '\0') ||
	    (filePath[0] == '.' && filePath[1] == '/' && filePath[2] == '\0'))
	{
		SafeSnprinf(bufferOut, bufferSize, "%s", filePath);
		return;
	}

	const char* workingDirAbsolute = realpath(".", nullptr);
	if (!workingDirAbsolute)
	{
		SafeSnprinf(bufferOut, bufferSize, "%s", filePath);
		return;
	}

	const char* filePathAbsolute = realpath(filePath, nullptr);
	if (!filePathAbsolute)
	{
		free((void*)workingDirAbsolute);
		SafeSnprinf(bufferOut, bufferSize, "%s", filePath);
		return;
	}

	// Logf("workingDirAbsolute %s\nfilePathAbsolute %s\n", workingDirAbsolute, filePathAbsolute);

	size_t workingDirPathLength = strlen(workingDirAbsolute);
	if (strncmp(workingDirAbsolute, filePathAbsolute, workingDirPathLength) == 0)
	{
		// The resolved path is within working dir
		int trimTrailingSlash = filePathAbsolute[workingDirPathLength] == '/' ? 1 : 0;
		const char* startRelativePath = &filePathAbsolute[workingDirPathLength + trimTrailingSlash];
		SafeSnprinf(bufferOut, bufferSize, "%s", startRelativePath);
	}
	else
	{
		// Resolved path is above working dir
		// Could still make this relative with ../ up to differing directory, if I find it's desired
		SafeSnprinf(bufferOut, bufferSize, "%s", filePathAbsolute);
	}

	free((void*)workingDirAbsolute);
	free((void*)filePathAbsolute);
#else
#error Need to be able to normalize path on this platform
#endif

	// // Test example
	// {
	// 	const char* testCases[] = {"../gamelib/RunProfiler.sh",
	// 	                           "runtime/HotReloading.cake",
	// 	                           "runtime/../src/Evaluator.hpp",
	// 	                           "src/../badfile",
	// 	                           "ReadMe.org",
	// 							   ".", "./",
	// 	                           "/home/macoy/delme.txt"};
	// 	for (int i = 0; i < ArraySize(testCases); ++i)
	// 	{
	// 		char resultBuffer[MAX_PATH_LENGTH] = {0};
	// 		makeAbsoluteOrRelativeToWorkingDir(testCases[i], resultBuffer, ArraySize(resultBuffer));
	// 		Logf("%s = %s\n\n", testCases[i], resultBuffer);
	// 	}
	// 	return 0;
	// }
}

bool outputFilenameFromSourceFilename(const char* outputDir, const char* sourceFilename,
                                      const char* addExtension, char* bufferOut, int bufferSize)
{
	char buildFilename[MAX_NAME_LENGTH] = {0};
	getFilenameFromPath(sourceFilename, buildFilename, sizeof(buildFilename));
	if (!buildFilename[0])
		return false;

	// TODO: Trim .cake.cpp (etc.)
	if (!addExtension)
	{
		SafeSnprinf(bufferOut, bufferSize, "%s/%s", outputDir, buildFilename);
	}
	else
	{
		SafeSnprinf(bufferOut, bufferSize, "%s/%s.%s", outputDir, buildFilename, addExtension);
	}
	return true;
}

bool copyBinaryFileTo(const char* srcFilename, const char* destFilename)
{
	// Note: man 3 fopen says "b" is unnecessary on Linux, but I'll keep it anyways
	FILE* srcFile = fopen(srcFilename, "rb");
	FILE* destFile = fopen(destFilename, "wb");
	if (!srcFile || !destFile)
	{
		perror("fopen: ");
		Logf("error: failed to copy %s to %s", srcFilename, destFilename);
		return false;
	}

	char buffer[4096];
	while (fread(buffer, sizeof(buffer), 1, srcFile))
		fwrite(buffer, sizeof(buffer), 1, destFile);

	fclose(srcFile);
	fclose(destFile);

	if (log.fileSystem)
		Logf("Wrote %s\n", destFilename);

	return true;
}

bool copyFileTo(const char* srcFilename, const char* destFilename)
{
	FILE* srcFile = fopen(srcFilename, "r");
	FILE* destFile = fopen(destFilename, "w");
	if (!srcFile || !destFile)
	{
		perror("fopen: ");
		Logf("error: failed to copy %s to %s", srcFilename, destFilename);
		return false;
	}

	char buffer[4096];
	while (fgets(buffer, sizeof(buffer), srcFile))
		fputs(buffer, destFile);

	fclose(srcFile);
	fclose(destFile);

	if (log.fileSystem)
		Logf("Wrote %s\n", destFilename);

	return true;
}

bool moveFile(const char* srcFilename, const char* destFilename)
{
	if (!copyFileTo(srcFilename, destFilename))
		return false;

	if (remove(srcFilename) != 0)
	{
		perror("remove: ");
		Logf("Failed to remove %s\n", srcFilename);
		return false;
	}

	return true;
}

void addExecutablePermission(const char* filename)
{
#ifdef UNIX
	// From man 2 chmod:
	// S_IRUSR  (00400)  read by owner
	//  S_IWUSR  (00200)  write by owner
	//  S_IXUSR  (00100)  execute/search by owner ("search" applies for directories, and means
	// that entries within the directory can be accessed)
	//  S_IRGRP  (00040)  read by group
	//  S_IWGRP  (00020)  write by group
	//  S_IXGRP  (00010)  execute/search by group
	//  S_IROTH  (00004)  read by others
	//  S_IWOTH  (00002)  write by others
	//  S_IXOTH  (00001)  execute/search by others

	if (chmod(filename, S_IRUSR | S_IWUSR | S_IXUSR | S_IRGRP | S_IXGRP | S_IROTH | S_IXOTH) == -1)
	{
		perror("addExecutablePermission: ");
	}
#endif
}
