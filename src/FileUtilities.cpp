#include "FileUtilities.hpp"

#include <stdio.h>
#include <string.h>

#include "Logging.hpp"
#include "Utilities.hpp"

#if defined(UNIX) || defined(MACOS)
#include <errno.h>
#include <fcntl.h>
#include <libgen.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <unistd.h>

#elif WINDOWS
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#else
#error Need to implement file utilities for this platform
#endif

FileModifyTime fileGetLastModificationTime(const char* filename)
{
#if defined(UNIX) || defined(MACOS)
	struct stat fileStat;
	if (stat(filename, &fileStat) == -1)
	{
		if (logging.fileSystem || errno != ENOENT)
			perror("fileGetLastModificationTime: ");
		return 0;
	}

	return (FileModifyTime)fileStat.st_mtime;
#elif WINDOWS
	// Doesn't actually create new due to OPEN_EXISTING
	HANDLE hFile =
	    CreateFile(filename, GENERIC_READ, FILE_SHARE_READ, /*lpSecurityAttributes=*/nullptr,
	               OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, /*hTemplateFile=*/nullptr);
	if (hFile == INVALID_HANDLE_VALUE)
		return 0;

	FILETIME ftCreate, ftAccess, ftWrite;
	if (!GetFileTime(hFile, &ftCreate, &ftAccess, &ftWrite))
	{
		CloseHandle(hFile);
		return 0;
	}

	CloseHandle(hFile);

	ULARGE_INTEGER lv_Large;
	lv_Large.LowPart = ftWrite.dwLowDateTime;
	lv_Large.HighPart = ftWrite.dwHighDateTime;

	FileModifyTime ftWriteTime = (FileModifyTime)lv_Large.QuadPart;
	if (ftWriteTime < 0)
		return 0;
	return ftWriteTime;
#else
	return 0;
#endif
}

bool fileIsMoreRecentlyModified(const char* filename, const char* reference)
{
#if defined(UNIX) || defined(MACOS)
	struct stat fileStat;
	struct stat referenceStat;
	if (stat(filename, &fileStat) == -1)
	{
		if (logging.fileSystem || errno != ENOENT)
			perror("fileIsMoreRecentlyModified: ");
		return true;
	}
	if (stat(reference, &referenceStat) == -1)
	{
		if (logging.fileSystem || errno != ENOENT)
			perror("fileIsMoreRecentlyModified: ");
		return true;
	}

	// Logf("%s vs %s: %lu %lu\n", filename, reference, fileStat.st_mtime, referenceStat.st_mtime);

	return fileStat.st_mtime > referenceStat.st_mtime;
#elif WINDOWS
	// Doesn't actually create new due to OPEN_EXISTING
	HANDLE hFile =
	    CreateFile(filename, GENERIC_READ, FILE_SHARE_READ, /*lpSecurityAttributes=*/nullptr,
	               OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, /*hTemplateFile=*/nullptr);
	if (hFile == INVALID_HANDLE_VALUE)
		return true;
	HANDLE hReference =
	    CreateFile(reference, GENERIC_READ, FILE_SHARE_READ, /*lpSecurityAttributes=*/nullptr,
	               OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, /*hTemplateFile=*/nullptr);
	if (hReference == INVALID_HANDLE_VALUE)
	{
		CloseHandle(hFile);
		return true;
	}
	FILETIME ftCreate, ftAccess, ftWrite;
	if (!GetFileTime(hFile, &ftCreate, &ftAccess, &ftWrite))
	{
		CloseHandle(hFile);
		CloseHandle(hReference);
		return true;
	}
	FILETIME ftCreateRef, ftAccessRef, ftWriteRef;
	if (!GetFileTime(hReference, &ftCreateRef, &ftAccessRef, &ftWriteRef))
	{
		CloseHandle(hFile);
		CloseHandle(hReference);
		return true;
	}

	CloseHandle(hFile);
	CloseHandle(hReference);
	return CompareFileTime(&ftWrite, &ftWriteRef) >= 1;

	return true;
#else
	// Err on the side of filename always being newer than the reference. The #error in the includes
	// block should prevent this from ever being compiled anyways
	return true;
#endif
}

bool fileExists(const char* filename)
{
#if defined(UNIX) || defined(MACOS)
	return access(filename, F_OK) != -1;
#elif WINDOWS
	return GetFileAttributes(filename) != INVALID_FILE_ATTRIBUTES;
#else
	return false;
#endif
}

bool makeDirectory(const char* path)
{
#if defined(UNIX) || defined(MACOS)
	if (mkdir(path, 0755) == -1)
	{
		// We don't care about EEXIST, we just want the dir
		if (logging.fileSystem || errno != EEXIST)
			perror("makeDirectory: ");
		if (errno != EEXIST)
			return false;
	}
#elif WINDOWS
	if (!CreateDirectory(path, /*lpSecurityAttributes=*/nullptr))
	{
		if (GetLastError() != ERROR_ALREADY_EXISTS)
		{
			Logf("makeDirectory failed to make %s", path);
			return false;
		}
	}
#else
#error Need to be able to make directories on this platform
#endif

	return true;
}

void getDirectoryFromPath(const char* path, char* bufferOut, int bufferSize)
{
#if defined(UNIX) || defined(MACOS)
	char* pathCopy = StrDuplicate(path);
	const char* dirName = dirname(pathCopy);
	SafeSnprintf(bufferOut, bufferSize, "%s", dirName);
	free(pathCopy);
#elif WINDOWS
	char drive[_MAX_DRIVE];
	char dir[_MAX_DIR];
	// char fname[_MAX_FNAME];
	// char ext[_MAX_EXT];
	_splitpath_s(path, drive, sizeof(drive), dir, sizeof(dir),
	             /*fname=*/nullptr, 0,
	             /*ext=*/nullptr, 0);
	_makepath_s(bufferOut, bufferSize, drive, dir, /*fname=*/nullptr,
	            /*ext=*/nullptr);
#else
#error Need to be able to strip file from path to get directory on this platform
#endif
}

void getFilenameFromPath(const char* path, char* bufferOut, int bufferSize)
{
#if defined(UNIX) || defined(MACOS)
	char* pathCopy = StrDuplicate(path);
	const char* fileName = basename(pathCopy);
	SafeSnprintf(bufferOut, bufferSize, "%s", fileName);
	free(pathCopy);
#elif WINDOWS
	// char drive[_MAX_DRIVE];
	// char dir[_MAX_DIR];
	char fname[_MAX_FNAME];
	char ext[_MAX_EXT];
	_splitpath_s(path, /*drive=*/nullptr, 0, /*dir=*/nullptr, 0, fname, sizeof(fname), ext,
	             sizeof(ext));
	_makepath_s(bufferOut, bufferSize, /*drive=*/nullptr, /*dir=*/nullptr, fname, ext);
#else
#error Need to be able to strip path to get filename on this platform
#endif
}

void makePathRelativeToFile(const char* filePath, const char* referencedFilePath, char* bufferOut,
                            int bufferSize)
{
	getDirectoryFromPath(filePath, bufferOut, bufferSize);
	StrCatSafe(bufferOut, bufferSize, "/");
	StrCatSafe(bufferOut, bufferSize, referencedFilePath);
}

const char* makeAbsolutePath_Allocated(const char* fromDirectory, const char* filePath)
{
#if defined(UNIX) || defined(MACOS)
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
#elif WINDOWS
	char* absolutePath = (char*)calloc(MAX_PATH_LENGTH, sizeof(char));
	bool isValid = false;
	if (fromDirectory)
	{
		char relativePath[MAX_PATH_LENGTH] = {0};
		PrintfBuffer(relativePath, "%s/%s", fromDirectory, filePath);
		isValid = _fullpath(absolutePath, relativePath, MAX_PATH_LENGTH);
	}
	else
	{
		isValid = _fullpath(absolutePath, filePath, MAX_PATH_LENGTH);
	}

	if (!isValid)
	{
		free(absolutePath);
		return nullptr;
	}
	return absolutePath;
#else
#error Need to be able to normalize path on this platform
	return nullptr;
#endif
}

void makeAbsoluteOrRelativeToWorkingDir(const char* filePath, char* bufferOut, int bufferSize)
{
#if defined(UNIX) || defined(MACOS)
	// If it's already absolute, keep it that way
	// Accept a lone . as well, for current working directory
	if (filePath[0] == '/' || (filePath[0] == '.' && filePath[1] == '\0') ||
	    (filePath[0] == '.' && filePath[1] == '/' && filePath[2] == '\0'))
	{
		SafeSnprintf(bufferOut, bufferSize, "%s", filePath);
		return;
	}

	const char* workingDirAbsolute = realpath(".", nullptr);
	if (!workingDirAbsolute)
	{
		SafeSnprintf(bufferOut, bufferSize, "%s", filePath);
		return;
	}

	const char* filePathAbsolute = realpath(filePath, nullptr);
	if (!filePathAbsolute)
	{
		free((void*)workingDirAbsolute);
		SafeSnprintf(bufferOut, bufferSize, "%s", filePath);
		return;
	}

	// Logf("workingDirAbsolute %s\nfilePathAbsolute %s\n", workingDirAbsolute, filePathAbsolute);

	size_t workingDirPathLength = strlen(workingDirAbsolute);
	size_t filePathLength = strlen(filePathAbsolute);
	if (strncmp(workingDirAbsolute, filePathAbsolute, workingDirPathLength) == 0 &&
	    filePathLength > workingDirPathLength && filePathAbsolute[workingDirPathLength + 1] == '/')
	{
		// The resolved path is within working dir
		int trimTrailingSlash = filePathAbsolute[workingDirPathLength] == '/' ? 1 : 0;
		const char* startRelativePath = &filePathAbsolute[workingDirPathLength + trimTrailingSlash];
		SafeSnprintf(bufferOut, bufferSize, "%s", startRelativePath);
	}
	else
	{
		// Resolved path is above working dir. I could still make this relative with ../ up to
		// differing directory, if I find it's desired. For now, keep it relative, even if
		// concatenated relative paths start to get hard to read
		SafeSnprintf(bufferOut, bufferSize, "%s", filePath);
	}

	free((void*)workingDirAbsolute);
	free((void*)filePathAbsolute);
#elif WINDOWS
	char drive[_MAX_DRIVE];
	char dir[_MAX_DIR];
	char fname[_MAX_FNAME];
	char ext[_MAX_EXT];
	_splitpath_s(filePath, drive, sizeof(drive), dir, sizeof(dir), fname, sizeof(fname), ext,
	             sizeof(ext));
	// If it's already absolute, keep it that way
	if (drive[0])
	{
		_makepath_s(bufferOut, bufferSize, drive, dir, fname, ext);
	}
	else
	{
		char workingDirAbsolute[MAX_PATH_LENGTH] = {0};
		GetCurrentDirectory(sizeof(workingDirAbsolute), workingDirAbsolute);
		const char* filePathAbsolute = makeAbsolutePath_Allocated(nullptr, filePath);
		if (!filePathAbsolute)
		{
			SafeSnprintf(bufferOut, bufferSize, "%s", filePath);
			return;
		}

		// Within the same directory?
		size_t workingDirPathLength = strlen(workingDirAbsolute);
		size_t filePathLength = strlen(filePathAbsolute);
		if (strncmp(workingDirAbsolute, filePathAbsolute, workingDirPathLength) == 0 &&
		    filePathLength > workingDirPathLength &&
		    (filePathAbsolute[workingDirPathLength + 1] == '/' ||
		     filePathAbsolute[workingDirPathLength + 1] == '\\'))
		{
			// The resolved path is within working dir
			int trimTrailingSlash = filePathAbsolute[workingDirPathLength] == '/' ||
			                                filePathAbsolute[workingDirPathLength] == '\\' ?
			                            1 :
			                            0;
			const char* startRelativePath =
			    &filePathAbsolute[workingDirPathLength + trimTrailingSlash];
			SafeSnprintf(bufferOut, bufferSize, "%s", startRelativePath);
		}
		else
		{
			// Resolved path is above working dir. I could still make this relative with ../ up to
		// differing directory, if I find it's desired. For now, keep it relative, even if
		// concatenated relative paths start to get hard to read
			SafeSnprintf(bufferOut, bufferSize, "%s", filePath);
		}
		free((void*)filePathAbsolute);
	}
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
		SafeSnprintf(bufferOut, bufferSize, "%s/%s", outputDir, buildFilename);
	}
	else
	{
		SafeSnprintf(bufferOut, bufferSize, "%s/%s.%s", outputDir, buildFilename, addExtension);
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
		Logf("error: failed to copy %s to %s\n", srcFilename, destFilename);
		return false;
	}

	char buffer[4096];
	size_t totalCopied = 0;
	size_t numRead = fread(buffer, sizeof(buffer[0]), ArraySize(buffer), srcFile);
	while (numRead)
	{
		fwrite(buffer, sizeof(buffer[0]), numRead, destFile);
		totalCopied += numRead;
		numRead = fread(buffer, sizeof(buffer[0]), ArraySize(buffer), srcFile);
	}

	if (logging.fileSystem)
		Logf(FORMAT_SIZE_T " bytes copied\n", totalCopied);

	fclose(srcFile);
	fclose(destFile);

	if (logging.fileSystem)
		Logf("Wrote %s\n", destFilename);

	return true;
}

bool copyFileTo(const char* srcFilename, const char* destFilename)
{
	FILE* srcFile = fopen(srcFilename, "rb");
	FILE* destFile = fopen(destFilename, "wb");
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

	if (logging.fileSystem)
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
	// Not necessary on Windows
#if defined(UNIX) || defined(MACOS)
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

void makeBackslashFilename(char* buffer, int bufferSize, const char* filename)
{
	char* bufferWrite = buffer;
	for (const char* currentChar = filename; *currentChar; ++currentChar)
	{
		if (*currentChar == '/')
			*bufferWrite = '\\';
		else
			*bufferWrite = *currentChar;

		++bufferWrite;
		if (bufferWrite - buffer >= bufferSize)
		{
			Log("error: could not make safe filename: buffer too small\n");
			break;
		}
	}
}

// TODO: Safer version
bool changeExtension(char* buffer, const char* newExtension)
{
	int bufferLength = strlen(buffer);
	char* expectExtensionStart = nullptr;
	for (char* currentChar = buffer + (bufferLength - 1); *currentChar && currentChar > buffer;
	     --currentChar)
	{
		if (*currentChar == '.')
		{
			expectExtensionStart = currentChar;
			break;
		}
	}
	if (!expectExtensionStart)
		return false;

	char* extensionWrite = expectExtensionStart + 1;
	for (const char* extensionChar = newExtension; *extensionChar; ++extensionChar)
	{
		*extensionWrite = *extensionChar;
		++extensionWrite;
	}
	*extensionWrite = '\0';
	return true;
}
