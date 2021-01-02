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

	// Default is to just keep it as-is. This may make sense to bolster in the future via using
	// search paths, e.g. remove the need to specify /usr/bin
	SafeSnprintf(resolvedPathOut, resolvedPathOutSize, "%s", fileToExecute);
	return true;
}
