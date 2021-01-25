#include "Build.hpp"

#include "FileUtilities.hpp"
#include "Logging.hpp"
#include "Utilities.hpp"

#ifdef WINDOWS
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
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
	SafeSnprintf(buffer, bufferSize, "/I \"%s\"", searchDir);
#else
	SafeSnprintf(buffer, bufferSize, "-I%s", searchDir);
#endif
}

void makeObjectOutputArgument(char* buffer, int bufferSize, const char* objectName)
{
	// TODO: Make this a setting rather than a define
#ifdef WINDOWS
	SafeSnprintf(buffer, bufferSize, "/Fo\"%s\"", objectName);
#else
	SafeSnprintf(buffer, bufferSize, "%s", objectName);
#endif
}

void makeDynamicLibraryOutputArgument(char* buffer, int bufferSize, const char* libraryName,
                                      const char* buildExecutable)
{
	if (StrCompareIgnoreCase(buildExecutable, "cl.exe") == 0)
	{
		Log("error: attempting to build dynamic library using cl.exe. You must use link.exe "
		    "instead\n");
		SafeSnprintf(buffer, bufferSize, "%s", libraryName);
	}
	else if (StrCompareIgnoreCase(buildExecutable, "link.exe") == 0)
	{
		SafeSnprintf(buffer, bufferSize, "/OUT:\"%s\"", libraryName);
	}
	else
	{
		SafeSnprintf(buffer, bufferSize, "%s", libraryName);
	}
}

void makeExecutableOutputArgument(char* buffer, int bufferSize, const char* executableName,
                                  const char* linkExecutable)
{
	// Annoying exception for MSVC not having spaces between some arguments
	if (StrCompareIgnoreCase(linkExecutable, "cl.exe") == 0)
	{
		SafeSnprintf(buffer, bufferSize, "/Fe\"%s\"", executableName);
	}
	else if (StrCompareIgnoreCase(linkExecutable, "link.exe") == 0)
	{
		SafeSnprintf(buffer, bufferSize, "/out:\"%s\"", executableName);
	}
	else
	{
		SafeSnprintf(buffer, bufferSize, "%s", executableName);
	}
}

void makeLinkLibraryArgument(char* buffer, int bufferSize, const char* libraryName,
                             const char* linkExecutable)
{
	if (StrCompareIgnoreCase(linkExecutable, "cl.exe") == 0)
	{
		SafeSnprintf(buffer, bufferSize, "%s.dll", libraryName);
	}
	else if (StrCompareIgnoreCase(linkExecutable, "link.exe") == 0)
	{
		SafeSnprintf(buffer, bufferSize, "%s.dll", libraryName);
	}
	else
	{
		SafeSnprintf(buffer, bufferSize, "-l%s", libraryName);
	}
}

void makeLinkLibrarySearchDirArgument(char* buffer, int bufferSize, const char* searchDir,
                                      const char* linkExecutable)
{
	if (StrCompareIgnoreCase(linkExecutable, "cl.exe") == 0)
	{
		SafeSnprintf(buffer, bufferSize, "/LIBPATH:%s", searchDir);
	}
	else if (StrCompareIgnoreCase(linkExecutable, "link.exe") == 0)
	{
		SafeSnprintf(buffer, bufferSize, "/LIBPATH:%s", searchDir);
	}
	else
	{
		SafeSnprintf(buffer, bufferSize, "-L%s", searchDir);
	}
}

void makeLinkLibraryRuntimeSearchDirArgument(char* buffer, int bufferSize, const char* searchDir,
                                             const char* linkExecutable)
{
	if (StrCompareIgnoreCase(linkExecutable, "cl.exe") == 0)
	{
		// TODO: Decide how to handle DLLs on Windows. Copy to same dir? Convert to full paths?
		// SafeSnprintf(buffer, bufferSize, "/LIBPATH:%s", searchDir);
		Log("warning: link library runtime search directories not supported on Windows\n");
	}
	else if (StrCompareIgnoreCase(linkExecutable, "link.exe") == 0)
	{
		// SafeSnprintf(buffer, bufferSize, "/LIBPATH:%s", searchDir);
		Log("warning: link library runtime search directories not supported on Windows\n");
	}
	else
	{
		SafeSnprintf(buffer, bufferSize, "-Wl,-rpath,%s", searchDir);
	}
}

void makeLinkerArgument(char* buffer, int bufferSize, const char* argument,
                        const char* linkExecutable)
{
	if (StrCompareIgnoreCase(linkExecutable, "cl.exe") == 0)
	{
		SafeSnprintf(buffer, bufferSize, "%s", argument);
	}
	else if (StrCompareIgnoreCase(linkExecutable, "link.exe") == 0)
	{
		SafeSnprintf(buffer, bufferSize, "%s", argument);
	}
	else
	{
		SafeSnprintf(buffer, bufferSize, "-Wl,%s", argument);
	}
}

bool resolveExecutablePath(const char* fileToExecute, char* resolvedPathOut,
                           int resolvedPathOutSize)
{
#ifdef WINDOWS
	// We need to do some extra legwork to find which compiler they actually want to use, based on
	// the current environment variables set by vcvars*.bat
	// See https://docs.microsoft.com/en-us/cpp/build/building-on-the-command-line?view=msvc-160
	if (_stricmp(fileToExecute, "cl.exe") == 0 || _stricmp(fileToExecute, "link.exe") == 0)
	{
		LPTSTR vcInstallDir = nullptr;
		LPTSTR vcHostArchitecture = nullptr;
		LPTSTR vcTargetArchitecture = nullptr;
		const int bufferSize = 4096;
		struct
		{
			const char* variableName;
			LPTSTR* outputString;
		} msvcVariables[] = {{"VCToolsInstallDir", &vcInstallDir},
		                     {"VSCMD_ARG_HOST_ARCH", &vcHostArchitecture},
		                     {"VSCMD_ARG_TGT_ARCH", &vcTargetArchitecture}};
		bool variablesFound = true;
		for (int i = 0; i < ArraySize(msvcVariables); ++i)
		{
			*msvcVariables[i].outputString = (LPTSTR)malloc(bufferSize * sizeof(TCHAR));
			if (!GetEnvironmentVariable(msvcVariables[i].variableName,
			                            *msvcVariables[i].outputString, bufferSize))
			{
				Logf(
				    "error: could not find environment variable '%s'.\n Please read the "
				    "following "
				    "URL:\nhttps://docs.microsoft.com/en-us/cpp/build/"
				    "building-on-the-command-line?view=msvc-160\nYou must run Cakelisp in a "
				    "command prompt which has already run vcvars* scripts.\nSee "
				    "cakelisp/Build.bat for an example.\nYou can define variables when running "
				    "Cakelisp from Visual Studio via Project -> Properties -> Configuration "
				    "Properties -> Debugging -> Environment\n",
				    msvcVariables[i].variableName);

				Log("The following vars need to be defined in the environment to be read from "
				    "Cakelisp directly:\n");
				for (int n = 0; n < ArraySize(msvcVariables); ++n)
					Logf("\t%s\n", msvcVariables[n].variableName);
				Log("Note that MSVC relies on more variables which vcvars*.bat define, so you need "
				    "to define those as well (if you do not use vcvars script).\n");

				variablesFound = false;

				break;
			}
		}

		if (variablesFound)
		{
			// SafeSnprintf(resolvedPathOut, resolvedPathOutSize, "%sHost%s/%s/%s", vcInstallDir,
			// vcHostArchitecture,
			SafeSnprintf(resolvedPathOut, resolvedPathOutSize, "%sbin\\Host%s\\%s\\%s",
			             vcInstallDir, vcHostArchitecture, vcTargetArchitecture, fileToExecute);

			if (logging.processes)
				Logf("\nOverriding command to:\n%s\n\n", resolvedPathOut);
		}

		for (int n = 0; n < ArraySize(msvcVariables); ++n)
			if (*msvcVariables[n].outputString)
				free(*msvcVariables[n].outputString);

		return variablesFound;
	}
#endif

	// Unix searches PATH automatically, thanks to the 'p' of execvp()
	SafeSnprintf(resolvedPathOut, resolvedPathOutSize, "%s", fileToExecute);

	return true;
}
