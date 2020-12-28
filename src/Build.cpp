#include "Build.hpp"

#include "Utilities.hpp"

#ifdef WINDOWS
const char* compilerObjectExtension = "obj";
const char* linkerDynamicLibraryPrefix = "";
const char* linkerDynamicLibraryExtension = "dll";
const char* defaultExecutableName = "output.exe";
#else
const char* compilerObjectExtension = "o";
const char* linkerDynamicLibraryPrefix = "lib";
const char* linkerDynamicLibraryExtension = "so";
const char* defaultExecutableName = "a.out";
#endif

void makeIncludeArgument(char* buffer, int bufferSize, const char* searchDir)
{
// TODO: Make this a setting rather than a define
#ifdef WINDOWS
	SafeSnprinf(buffer, bufferSize, "/I \"%s\"", searchDir);
#else
	SafeSnprinf(buffer, bufferSize, "-I%s", searchDir);
#endif
}

void makeObjectOutputArgument(char* buffer, int bufferSize, const char* objectName)
{
	// TODO: Make this a setting rather than a define
#ifdef WINDOWS
	SafeSnprinf(buffer, bufferSize, "/Fo\"%s\"", objectName);
#else
	SafeSnprinf(buffer, bufferSize, "%s", objectName);
#endif
}

void makeDynamicLibraryOutputArgument(char* buffer, int bufferSize, const char* libraryName,
                                      const char* buildExecutable)
{
	if (StrCompareIgnoreCase(buildExecutable, "cl.exe") == 0)
	{
		Log("error: attempting to build dynamic library using cl.exe. You must use link.exe "
		    "instead\n");
		SafeSnprinf(buffer, bufferSize, "%s", libraryName);
	}
	else if (StrCompareIgnoreCase(buildExecutable, "link.exe") == 0)
	{
		SafeSnprinf(buffer, bufferSize, "/OUT:\"%s\"", libraryName);
	}
	else
	{
		SafeSnprinf(buffer, bufferSize, "%s", libraryName);
	}
}
