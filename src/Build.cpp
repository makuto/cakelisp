#include "Build.hpp"

#include <string.h>

#include <cstring>
#include <vector>

#include "FileUtilities.hpp"
#include "GeneratorHelpers.hpp"
#include "Logging.hpp"
#include "ModuleManager.hpp"
#include "Tokenizer.hpp"
#include "Utilities.hpp"

#ifdef WINDOWS
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

// TODO: These should change based on compiler, i.e. you should be able to build things via mingw on
// Windows, using the Unix extensions
const char* compilerObjectExtension = "obj";
const char* compilerDebugSymbolsExtension = "pdb";
const char* compilerImportLibraryExtension = "lib";
const char* linkerDynamicLibraryPrefix = "";  // Not applicable
const char* linkerDynamicLibraryExtension = "dll";
const char* defaultExecutableName = "output.exe";
#else
const char* compilerObjectExtension = "o";
const char* compilerDebugSymbolsExtension = "";   // Not applicable
const char* compilerImportLibraryExtension = "";  // Not applicable
const char* linkerDynamicLibraryPrefix = "lib";
const char* linkerDynamicLibraryExtension = "so";
const char* defaultExecutableName = "a.out";
#endif

const char* precompiledHeaderExtension = "pch";

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

void makeImportLibraryPathArgument(char* buffer, int bufferSize, const char* path,
                                   const char* buildExecutable)
{
	if (StrCompareIgnoreCase(buildExecutable, "link.exe") == 0)
	{
		SafeSnprintf(buffer, bufferSize, "/LIBPATH:%s", path);
	}
	// else if (StrCompareIgnoreCase(buildExecutable, "link.exe") == 0)
	// {
	// 	SafeSnprintf(buffer, bufferSize, "/OUT:\"%s\"", path);
	// }
	else
	{
		SafeSnprintf(buffer, bufferSize, "%s", path);
	}
}

void makeDebugSymbolsOutputArgument(char* buffer, int bufferSize, const char* debugSymbolsName)
{
	// TODO: Make this a setting rather than a define
#ifdef WINDOWS
	SafeSnprintf(buffer, bufferSize, "/Fd\"%s\"", debugSymbolsName);
// #else // No repositioning of debug symbols necessary
// SafeSnprintf(buffer, bufferSize, "%s", debugSymbolsName);
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

void makePrecompiledHeaderOutputArgument(char* buffer, int bufferSize, const char* executableName,
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

void convertBuildArguments(BuildArgumentConverter* argumentsToConvert, int numArgumentsToConvert,
                           const char* buildExecutable)
{
	for (int typeIndex = 0; typeIndex < numArgumentsToConvert; ++typeIndex)
	{
		int numStrings = (int)argumentsToConvert[typeIndex].stringsIn->size();
		argumentsToConvert[typeIndex].argumentsOutMemory.resize(numStrings);
		argumentsToConvert[typeIndex].argumentsOut->resize(numStrings);

		int currentString = 0;
		for (const std::string& stringIn : *argumentsToConvert[typeIndex].stringsIn)
		{
			if (argumentsToConvert[typeIndex].argumentConversionFunc)
			{
				// Give some extra padding for prefixes
				char argumentOut[MAX_PATH_LENGTH + 15] = {0};
				argumentsToConvert[typeIndex].argumentConversionFunc(
				    argumentOut, sizeof(argumentOut), stringIn.c_str(), buildExecutable);
				argumentsToConvert[typeIndex].argumentsOutMemory[currentString] = argumentOut;
			}
			else
				argumentsToConvert[typeIndex].argumentsOutMemory[currentString] = stringIn;

			++currentString;
		}

		for (int stringIndex = 0; stringIndex < numStrings; ++stringIndex)
			(*argumentsToConvert[typeIndex].argumentsOut)[stringIndex] =
			    argumentsToConvert[typeIndex].argumentsOutMemory[stringIndex].c_str();
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

void buildWriteCacheFile(const char* buildOutputDir, ArtifactCrcTable& cachedCommandCrcs,
                         ArtifactCrcTable& newCommandCrcs)
{
	char outputFilename[MAX_PATH_LENGTH] = {0};
	if (!outputFilenameFromSourceFilename(buildOutputDir, "Cache", "cake", outputFilename,
	                                      sizeof(outputFilename)))
	{
		Log("error: failed to create cache file name\n");
		return;
	}

	// Combine all CRCs into a single map
	ArtifactCrcTable outputCrcs;
	for (ArtifactCrcTablePair& crcPair : cachedCommandCrcs)
		outputCrcs.insert(crcPair);
	// New commands override previously cached
	for (ArtifactCrcTablePair& crcPair : newCommandCrcs)
		outputCrcs[crcPair.first] = crcPair.second;

	std::vector<Token> outputTokens;
	const Token openParen = {TokenType_OpenParen, EmptyString, "Build.cpp", 1, 0, 0};
	const Token closeParen = {TokenType_CloseParen, EmptyString, "Build.cpp", 1, 0, 0};
	const Token crcInvoke = {TokenType_Symbol, "command-crc", "Build.cpp", 1, 0, 0};

	for (ArtifactCrcTablePair& crcPair : outputCrcs)
	{
		outputTokens.push_back(openParen);
		outputTokens.push_back(crcInvoke);

		Token artifactName = {TokenType_String, crcPair.first, "Build.cpp", 1, 0, 0};
		outputTokens.push_back(artifactName);

		Token crcToken = {TokenType_Symbol, std::to_string(crcPair.second), "Build.cpp", 1, 0, 0};
		outputTokens.push_back(crcToken);

		outputTokens.push_back(closeParen);
	}

	if (outputTokens.empty())
	{
		Log("no tokens to write to cache file");
		return;
	}

	FILE* file = fileOpen(outputFilename, "w");
	if (!file)
	{
		Logf("error: Could not write cache file %s", outputFilename);
		return;
	}

	prettyPrintTokensToFile(file, outputTokens);

	fclose(file);
}

// Returns false if there were errors; the file not existing is not an error
bool buildReadCacheFile(const char* buildOutputDir, ArtifactCrcTable& cachedCommandCrcs)
{
	char inputFilename[MAX_PATH_LENGTH] = {0};
	if (!outputFilenameFromSourceFilename(buildOutputDir, "Cache", "cake", inputFilename,
	                                      sizeof(inputFilename)))
	{
		Log("error: failed to create cache file name\n");
		return false;
	}

	// This is fine if it's the first build of this configuration
	if (!fileExists(inputFilename))
		return true;

	const std::vector<Token>* tokens = nullptr;
	if (!moduleLoadTokenizeValidate(inputFilename, &tokens))
	{
		// moduleLoadTokenizeValidate deletes tokens on error
		return false;
	}

	for (int i = 0; i < (int)(*tokens).size(); ++i)
	{
		const Token& currentToken = (*tokens)[i];
		if (currentToken.type == TokenType_OpenParen)
		{
			int endInvocationIndex = FindCloseParenTokenIndex((*tokens), i);
			const Token& invocationToken = (*tokens)[i + 1];
			if (invocationToken.contents.compare("command-crc") == 0)
			{
				int artifactIndex = getExpectedArgument("expected artifact name", (*tokens), i, 1,
				                                        endInvocationIndex);
				if (artifactIndex == -1)
				{
					delete tokens;
					return false;
				}
				int crcIndex =
				    getExpectedArgument("expected crc", (*tokens), i, 2, endInvocationIndex);
				if (crcIndex == -1)
				{
					delete tokens;
					return false;
				}

				char* endPtr;
				cachedCommandCrcs[(*tokens)[artifactIndex].contents] =
				    static_cast<uint32_t>(std::stoul((*tokens)[crcIndex].contents));
			}
			else
			{
				Logf("error: unrecognized invocation in %s: %s\n", inputFilename,
				     invocationToken.contents.c_str());
				delete tokens;
				return false;
			}

			i = endInvocationIndex;
		}
	}

	delete tokens;
	return true;
}

// It is essential to scan the #include files to determine if any of the headers have been modified,
// because changing them could require a rebuild (for e.g., you change the size or order of a struct
// declared in a header; all source files now need updated sizeof calls). This is annoyingly
// complex, but must be done every time a build is run, just in case headers change. If this step
// was skipped, it opens the door to very frustrating bugs involving stale builds and mismatched
// headers.
//
// Note that this cannot early out because we have no reference file to compare against. While it
// could be faster to implement it that way, my gut tells me having a cache sharable across build
// objects is faster. We must find the absolute time because different build objects may be more
// recently modified than others, so they shouldn't get built. If we wanted to early out, we cannot
// share the cache because of this
static FileModifyTime GetMostRecentIncludeModified_Recursive(
    const std::vector<std::string>& searchDirectories, const char* filename,
    const char* includedInFile, HeaderModificationTimeTable& isModifiedCache)
{
	// Already cached?
	{
		const HeaderModificationTimeTable::iterator findIt = isModifiedCache.find(filename);
		if (findIt != isModifiedCache.end())
		{
			if (logging.includeScanning)
				Logf("    > cache hit %s\n", filename);
			return findIt->second;
		}
	}

	// Find a match
	char resolvedPathBuffer[MAX_PATH_LENGTH] = {0};
	if (!searchForFileInPaths(filename, includedInFile, searchDirectories, resolvedPathBuffer,
	                          ArraySize(resolvedPathBuffer)))
	{
		if (logging.includeScanning || logging.strictIncludes)
			Logf("warning: failed to find %s in search paths\n", filename);

		// Might as well not keep failing to find it. It doesn't cause modification up the stream if
		// not found (else you'd get full rebuilds every time if you had even one header not found)
		isModifiedCache[filename] = 0;

		return 0;
	}

	// Is the resolved path cached?
	{
		const HeaderModificationTimeTable::iterator findIt =
		    isModifiedCache.find(resolvedPathBuffer);
		if (findIt != isModifiedCache.end())
			return findIt->second;
	}

	if (logging.includeScanning)
		Logf("Checking %s for headers\n", resolvedPathBuffer);

	const FileModifyTime thisModificationTime = fileGetLastModificationTime(resolvedPathBuffer);

	// To prevent loops, add ourselves to the cache now. We'll revise our answer higher if necessary
	isModifiedCache[filename] = thisModificationTime;

	FileModifyTime mostRecentModTime = thisModificationTime;

	FILE* file = fileOpen(resolvedPathBuffer, "r");
	if (!file)
		return false;
	char lineBuffer[2048] = {0};
	while (fgets(lineBuffer, sizeof(lineBuffer), file))
	{
		// I think '#   include' is valid
		if (lineBuffer[0] != '#' || !strstr(lineBuffer, "include"))
			continue;

		char foundInclude[MAX_PATH_LENGTH] = {0};
		char* foundIncludeWrite = foundInclude;
		bool foundOpening = false;
		for (char* c = &lineBuffer[0]; *c != '\0'; ++c)
		{
			if (foundOpening)
			{
				if (*c == '\"' || *c == '>')
				{
					if (logging.includeScanning)
						Logf("\t%s include: %s\n", resolvedPathBuffer, foundInclude);

					FileModifyTime includeModifiedTime = GetMostRecentIncludeModified_Recursive(
					    searchDirectories, foundInclude, resolvedPathBuffer, isModifiedCache);

					if (logging.includeScanning)
						Logf("\t tree modificaiton time: %lu\n", includeModifiedTime);

					if (includeModifiedTime > mostRecentModTime)
						mostRecentModTime = includeModifiedTime;
				}

				*foundIncludeWrite = *c;
				++foundIncludeWrite;
			}
			else if (*c == '\"' || *c == '<')
				foundOpening = true;
		}
	}

	if (thisModificationTime != mostRecentModTime)
		isModifiedCache[filename] = mostRecentModTime;

	fclose(file);

	return mostRecentModTime;
}

// commandArguments should have terminating null sentinel
bool commandEqualsCachedCommand(ArtifactCrcTable& cachedCommandCrcs, const char* artifactKey,
                                const char** commandArguments, uint32_t* crcOut)
{
	uint32_t newCommandCrc = 0;
	if (logging.commandCrcs)
		Log("\"");
	for (const char** currentArg = commandArguments; *currentArg; ++currentArg)
	{
		crc32(*currentArg, strlen(*currentArg), &newCommandCrc);
		if (logging.commandCrcs)
			Logf("%s ", *currentArg);
	}
	if (logging.commandCrcs)
		Log("\"\n");

	if (crcOut)
		*crcOut = newCommandCrc;

	ArtifactCrcTable::iterator findIt = cachedCommandCrcs.find(artifactKey);
	if (findIt == cachedCommandCrcs.end())
	{
		if (logging.commandCrcs)
			Logf("CRC32 for %s: %u (not cached)\n", artifactKey, newCommandCrc);
		return false;
	}

	if (logging.commandCrcs)
		Logf("CRC32 for %s: old %u new %u\n", artifactKey, findIt->second, newCommandCrc);

	return findIt->second == newCommandCrc;
}

bool cppFileNeedsBuild(EvaluatorEnvironment& environment, const char* sourceFilename,
                       const char* artifactFilename, const char** commandArguments,
                       ArtifactCrcTable& cachedCommandCrcs, ArtifactCrcTable& newCommandCrcs,
                       HeaderModificationTimeTable& headerModifiedCache,
                       std::vector<std::string>& headerSearchDirectories)
{
	uint32_t commandCrc = 0;
	bool commandEqualsCached = commandEqualsCachedCommand(cachedCommandCrcs, artifactFilename,
	                                                      commandArguments, &commandCrc);
	// We could avoid doing this work, but it makes it easier to log if we do it regardless of
	// commandEqualsCached invalidating our cache anyways
	bool canUseCache = canUseCachedFile(environment, sourceFilename, artifactFilename);
	bool headersModified = false;
	if (commandEqualsCached && canUseCache)
	{
		// Note that I use the .o as "includedBy" because our header may not have needed any
		// changes if our include changed. We have to use the .o as the time reference that
		// we've rebuilt
		FileModifyTime mostRecentHeaderModTime =
		    GetMostRecentIncludeModified_Recursive(headerSearchDirectories, sourceFilename,
		                                           /*includedBy*/ nullptr, headerModifiedCache);

		FileModifyTime artifactModTime = fileGetLastModificationTime(artifactFilename);
		if (artifactModTime >= mostRecentHeaderModTime)
		{
			if (logging.buildProcess)
				Logf("Skipping compiling %s (using cached object)\n", sourceFilename);
			return false;
		}
		else
		{
			headersModified = true;
			if (logging.includeScanning || logging.buildProcess)
			{
				Logf("--- Must rebuild %s (header files modified)\n", sourceFilename);
				Logf("Artifact: %lu Most recent header: %lu\n", artifactModTime,
				     mostRecentHeaderModTime);
			}
		}
	}

	if (logging.buildReasons)
	{
		Logf("Build %s reason(s):\n", artifactFilename);
		if (!canUseCache)
			Log("\tobject files updated\n");
		if (!commandEqualsCached)
			Log("\tcommand changed since last run\n");
		if (headersModified)
			Log("\theaders modified\n");
	}

	if (!commandEqualsCached)
		newCommandCrcs[artifactFilename] = commandCrc;

	return true;
}
