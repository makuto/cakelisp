#include "FileUtilities.hpp"

#include <stdio.h>
#include <string.h>

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

bool fileExists(const char* filename)
{
	return access(filename, F_OK) != -1;
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

	// printf("workingDirAbsolute %s\nfilePathAbsolute %s\n", workingDirAbsolute, filePathAbsolute);

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
	// 		printf("%s = %s\n\n", testCases[i], resultBuffer);
	// 	}
	// 	return 0;
	// }
}

// From https://stackoverflow.com/questions/2180079/how-can-i-copy-a-file-on-unix-using-c
// (I don't want to think about this right now)
int copyFile(const char* to, const char* from)
{
	int fd_to, fd_from;
	char buf[4096];
	ssize_t nread;
	int saved_errno;

	fd_from = open(from, O_RDONLY);
	if (fd_from < 0)
		return -1;

	fd_to = open(to, O_WRONLY | O_EXCL, 0666);
	if (fd_to < 0)
		goto out_error;

	while (nread = read(fd_from, buf, sizeof buf), nread > 0)
	{
		char* out_ptr = buf;
		ssize_t nwritten;

		do
		{
			nwritten = write(fd_to, out_ptr, nread);

			if (nwritten >= 0)
			{
				nread -= nwritten;
				out_ptr += nwritten;
			}
			else if (errno != EINTR)
			{
				goto out_error;
			}
		} while (nread > 0);
	}

	if (nread == 0)
	{
		if (close(fd_to) < 0)
		{
			fd_to = -1;
			goto out_error;
		}
		close(fd_from);

		/* Success! */
		return 0;
	}

out_error:
	saved_errno = errno;

	close(fd_from);
	if (fd_to >= 0)
		close(fd_to);

	errno = saved_errno;
	return -1;
}
