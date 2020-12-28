#include "ModuleManager.hpp"

#include <string.h>
#include <cstring>

#include "Build.hpp"
#include "Converters.hpp"
#include "DynamicLoader.hpp"
#include "Evaluator.hpp"
#include "EvaluatorEnums.hpp"
#include "FileUtilities.hpp"
#include "GeneratorHelpers.hpp"
#include "Generators.hpp"
#include "Logging.hpp"
#include "OutputPreambles.hpp"
#include "RunProcess.hpp"
#include "Tokenizer.hpp"
#include "Utilities.hpp"
#include "Writer.hpp"

// The ' symbols tell the signature validator that the actual contents of those symbols can be
// user-defined (just like C letting you specify arguments without names)
const char* g_modulePreBuildHookSignature =
    "('manager (& ModuleManager) 'module (* Module) &return bool)";

void listBuiltInGenerators()
{
	EvaluatorEnvironment environment;
	importFundamentalGenerators(environment);
	for (GeneratorIterator it = environment.generators.begin(); it != environment.generators.end();
	     ++it)
	{
		Logf("  %s\n", it->first.c_str());
	}
	environmentDestroyInvalidateTokens(environment);
}

void moduleManagerInitialize(ModuleManager& manager)
{
	importFundamentalGenerators(manager.environment);

	// Create module definition for top-level references to attach to
	// The token isn't actually tied to one file
	manager.globalPseudoInvocationName = {
	    TokenType_Symbol, globalDefinitionName, "global_pseudotarget", 1, 0, 1};
	{
		ObjectDefinition moduleDefinition = {};
		moduleDefinition.name = manager.globalPseudoInvocationName.contents;
		moduleDefinition.definitionInvocation = &manager.globalPseudoInvocationName;
		moduleDefinition.type = ObjectType_PseudoObject;
		moduleDefinition.isRequired = true;

		// The context on the definition shouldn't be used in this case unless this definition is
		// going to be reevaluated or replaced, which doesn't make much sense. Let's put stuff in
		// anyways, just to be sure
		{
			EvaluatorContext moduleContext = {};
			moduleContext.scope = EvaluatorScope_Module;
			moduleContext.definitionName = &manager.globalPseudoInvocationName;
			moduleContext.isRequired = true;
			moduleDefinition.context = moduleContext;
		}

		// Will be cleaned up when the environment is destroyed
		GeneratorOutput* compTimeOutput = new GeneratorOutput;
		moduleDefinition.output = compTimeOutput;
		if (!addObjectDefinition(manager.environment, moduleDefinition))
			Log("error: <global> couldn't be added. Was module manager initialized twice? Things "
			    "will definitely break\n");
	}

	manager.environment.moduleManager = &manager;

	// Command defaults
	{
#ifdef WINDOWS
		// MSVC by default
		// Our lives could be easier by using Clang or MinGW, but it wouldn't be the ideal for
		// hardcore Windows users, who we should support
		manager.environment.compileTimeBuildCommand.fileToExecute = "cl.exe";
		manager.environment.compileTimeBuildCommand.arguments = {
		    {ProcessCommandArgumentType_String, "/nologo"},
		    // Not 100% sure what the right default for this is
		    {ProcessCommandArgumentType_String, "/EHsc"},
		    // Need this to properly add declspec for importing symbols (on Linux, we don't need
		    // declspec, so it's ifdef'd based on platform)
		    {ProcessCommandArgumentType_String, "/DWINDOWS"},
		    // Need to use dynamic runtime so everything is shared. Cakelisp itself must be built
		    // with this matching as well (use just /MD for release) See
		    // https://stackoverflow.com/questions/22279052/c-passing-stdstring-by-reference-to-function-in-dll
		    {ProcessCommandArgumentType_String, "/MDd"},
			// Debug only
		    {ProcessCommandArgumentType_String, "/DEBUG:FASTLINK"},
		    {ProcessCommandArgumentType_String, "/Zi"},
		    {ProcessCommandArgumentType_String, "/c"},
		    {ProcessCommandArgumentType_SourceInput, EmptyString},
		    {ProcessCommandArgumentType_ObjectOutput, EmptyString},
		    {ProcessCommandArgumentType_CakelispHeadersInclude, EmptyString}};

		manager.environment.compileTimeLinkCommand.fileToExecute = "link.exe";
		manager.environment.compileTimeLinkCommand.arguments = {
		    {ProcessCommandArgumentType_String, "/nologo"},
		    {ProcessCommandArgumentType_String, "/DLL"},
		    // On Windows, .exes create .lib files for exports. Link it here so we don't get
		    // unresolved externals
		    {ProcessCommandArgumentType_String, "/LIBPATH:\"bin\""},
		    {ProcessCommandArgumentType_String, "cakelisp.lib"},
			// Debug only
			{ProcessCommandArgumentType_String, "/DEBUG:FASTLINK"},
			{ProcessCommandArgumentType_DynamicLibraryOutput, EmptyString},
		    {ProcessCommandArgumentType_ObjectInput, EmptyString}};

		manager.environment.buildTimeBuildCommand.fileToExecute = "cl.exe";
		manager.environment.buildTimeBuildCommand.arguments = {
		    {ProcessCommandArgumentType_String, "/nologo"},
		    {ProcessCommandArgumentType_String, "/EHsc"},
		    {ProcessCommandArgumentType_String, "/c"},
		    {ProcessCommandArgumentType_SourceInput, EmptyString},
		    {ProcessCommandArgumentType_ObjectOutput, EmptyString},
		    {ProcessCommandArgumentType_IncludeSearchDirs, EmptyString},
		    {ProcessCommandArgumentType_AdditionalOptions, EmptyString}};

		manager.environment.buildTimeLinkCommand.fileToExecute = "link.exe";
		manager.environment.buildTimeLinkCommand.arguments = {
		    {ProcessCommandArgumentType_String, "/nologo"},
		    {ProcessCommandArgumentType_ExecutableOutput, EmptyString},
		    {ProcessCommandArgumentType_ObjectInput, EmptyString}};
#else
		// G++ by default
		manager.environment.compileTimeBuildCommand.fileToExecute = "/usr/bin/g++";
		manager.environment.compileTimeBuildCommand.arguments = {
		    {ProcessCommandArgumentType_String, "-g"},
		    {ProcessCommandArgumentType_String, "-c"},
		    {ProcessCommandArgumentType_SourceInput, EmptyString},
		    {ProcessCommandArgumentType_String, "-o"},
		    {ProcessCommandArgumentType_ObjectOutput, EmptyString},
		    {ProcessCommandArgumentType_CakelispHeadersInclude, EmptyString},
		    {ProcessCommandArgumentType_String, "-fPIC"}};

		manager.environment.compileTimeLinkCommand.fileToExecute = "/usr/bin/g++";
		manager.environment.compileTimeLinkCommand.arguments = {
		    {ProcessCommandArgumentType_String, "-shared"},
		    {ProcessCommandArgumentType_String, "-o"},
		    {ProcessCommandArgumentType_DynamicLibraryOutput, EmptyString},
		    {ProcessCommandArgumentType_ObjectInput, EmptyString}};

		manager.environment.buildTimeBuildCommand.fileToExecute = "/usr/bin/g++";
		manager.environment.buildTimeBuildCommand.arguments = {
		    {ProcessCommandArgumentType_String, "-g"},
		    {ProcessCommandArgumentType_String, "-c"},
		    {ProcessCommandArgumentType_SourceInput, EmptyString},
		    {ProcessCommandArgumentType_String, "-o"},
		    {ProcessCommandArgumentType_ObjectOutput, EmptyString},
		    // Probably unnecessary to make the user's code position-independent, but it does make
		    // hotreloading a bit easier to try out
		    {ProcessCommandArgumentType_String, "-fPIC"},
		    {ProcessCommandArgumentType_IncludeSearchDirs, EmptyString},
		    {ProcessCommandArgumentType_AdditionalOptions, EmptyString}};

		manager.environment.buildTimeLinkCommand.fileToExecute = "/usr/bin/g++";
		manager.environment.buildTimeLinkCommand.arguments = {
		    {ProcessCommandArgumentType_String, "-o"},
		    {ProcessCommandArgumentType_ExecutableOutput, EmptyString},
		    {ProcessCommandArgumentType_ObjectInput, EmptyString}};
#endif
	}

	manager.environment.useCachedFiles = true;
	makeDirectory(cakelispWorkingDir);
	if (logging.fileSystem || logging.phases)
		Logf("Using cache at %s\n", cakelispWorkingDir);

	// By always searching relative to CWD, any subsequent imports with the module filename will
	// resolve correctly
	manager.environment.searchPaths.push_back(".");
}

void moduleManagerDestroy(ModuleManager& manager)
{
	environmentDestroyInvalidateTokens(manager.environment);
	for (Module* module : manager.modules)
	{
		delete module->tokens;
		delete module->generatedOutput;
		free((void*)module->filename);
		delete module;
	}
	manager.modules.clear();
	closeAllDynamicLibraries();
}

void makeSafeFilename(char* buffer, int bufferSize, const char* filename)
{
	char* bufferWrite = buffer;
	for (const char* currentChar = filename; *currentChar; ++currentChar)
	{
		if (*currentChar == '\\')
			*bufferWrite = '/';
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

bool moduleLoadTokenizeValidate(const char* filename, const std::vector<Token>** tokensOut)
{
	*tokensOut = nullptr;

	FILE* file = fileOpen(filename, "r");
	if (!file)
		return false;

	char lineBuffer[2048] = {0};
	int lineNumber = 1;
	// We need to be very careful about when we delete this so as to not invalidate pointers
	// It is immutable to also disallow any pointer invalidation if we were to resize it
	const std::vector<Token>* tokens = nullptr;
	{
		std::vector<Token>* tokens_CREATIONONLY = new std::vector<Token>;
		bool isFirstLine = true;
		while (fgets(lineBuffer, sizeof(lineBuffer), file))
		{
			if (logging.tokenization)
				Logf("%s", lineBuffer);

			// Check for shebang and ignore this line if found. This allows users to execute their
			// scripts via e.g. ./MyScript.cake, given #!/usr/bin/cakelisp --execute
			if (isFirstLine)
			{
				isFirstLine = false;
				if (lineBuffer[0] == '#' && lineBuffer[1] == '!')
				{
					if (logging.tokenization)
						Log("Skipping shebang\n");
					continue;
				}
			}

			const char* error =
			    tokenizeLine(lineBuffer, filename, lineNumber, *tokens_CREATIONONLY);
			if (error != nullptr)
			{
				Logf("%s:%d: error: %s\n", filename, lineNumber, error);

				delete tokens_CREATIONONLY;
				return false;
			}

			lineNumber++;
		}

		// Make it const to avoid pointer invalidation due to resize
		tokens = tokens_CREATIONONLY;
	}

	if (logging.tokenization)
		Logf("Tokenized %d lines\n", lineNumber - 1);

	if (tokens->empty())
	{
		Log("error: empty file. Please remove from system, or add (ignore)\n");
		delete tokens;
		return false;
	}

	if (!validateParentheses(*tokens))
	{
		delete tokens;
		return false;
	}

	if (logging.tokenization)
	{
		Log("\nResult:\n");

		// No need to validate, we already know it's safe
		int nestingDepth = 0;
		for (const Token& token : *tokens)
		{
			printIndentToDepth(nestingDepth);

			Logf("%s", tokenTypeToString(token.type));

			bool printRanges = true;
			if (printRanges)
			{
				Logf("\t\tline %d, from line character %d to %d\n", token.lineNumber,
				     token.columnStart, token.columnEnd);
			}

			if (token.type == TokenType_OpenParen)
			{
				++nestingDepth;
			}
			else if (token.type == TokenType_CloseParen)
			{
				--nestingDepth;
			}

			if (!token.contents.empty())
			{
				printIndentToDepth(nestingDepth);
				Logf("\t%s\n", token.contents.c_str());
			}
		}
	}

	fclose(file);

	*tokensOut = tokens;

	return true;
}

bool moduleManagerAddEvaluateFile(ModuleManager& manager, const char* filename, Module** moduleOut)
{
	if (moduleOut)
		*moduleOut = nullptr;

	if (!filename)
		return false;

	char resolvedPath[MAX_PATH_LENGTH] = {0};
	makeAbsoluteOrRelativeToWorkingDir(filename, resolvedPath, ArraySize(resolvedPath));
	char safePathBuffer[MAX_PATH_LENGTH] = {0};
	makeSafeFilename(safePathBuffer, sizeof(safePathBuffer), resolvedPath);

	const char* normalizedFilename = StrDuplicate(safePathBuffer);
	// Enabling this makes all file:line messages really long. For now, I'll keep it as relative to
	// current working directory of this executable.
	// const char* normalizedFilename = makeAbsolutePath_Allocated(".", filename);
	if (!normalizedFilename)
	{
#ifdef UNIX
		perror("failed to normalize filename: ");
#else
		Logf("error: could not normalize filename, or file not found: %s\n", filename);
#endif
		return false;
	}

	// Check for already loaded module. Make sure to use absolute paths to protect the user from
	// multiple includes in case they got tricky with their import path
	for (Module* module : manager.modules)
	{
		const char* normalizedProspectiveModuleFilename = makeAbsolutePath_Allocated(".", filename);
		if (!normalizedProspectiveModuleFilename)
		{
			Logf("error: failed to normalize path %s", filename);
			free((void*)normalizedFilename);
			return false;
		}

		const char* normalizedModuleFilename = makeAbsolutePath_Allocated(".", module->filename);

		if (!normalizedModuleFilename)
		{
			Logf("error: failed to normalize path %s", module->filename);
			free((void*)normalizedProspectiveModuleFilename);
			free((void*)normalizedFilename);
			return false;
		}

		if (strcmp(normalizedModuleFilename, normalizedProspectiveModuleFilename) == 0)
		{
			if (moduleOut)
				*moduleOut = module;

			if (logging.imports)
				Logf("Already loaded %s\n", normalizedFilename);
			free((void*)normalizedFilename);
			free((void*)normalizedProspectiveModuleFilename);
			free((void*)normalizedModuleFilename);
			return true;
		}

		free((void*)normalizedProspectiveModuleFilename);
		free((void*)normalizedModuleFilename);
	}

	Module* newModule = new Module();
	// We need to keep this memory around for the lifetime of the token, regardless of relocation
	newModule->filename = normalizedFilename;
	// This stage cleans up after itself if it fails
	if (!moduleLoadTokenizeValidate(newModule->filename, &newModule->tokens))
	{
		Logf("error: failed to tokenize %s\n", newModule->filename);
		delete newModule;
		free((void*)normalizedFilename);
		return false;
	}

	newModule->generatedOutput = new GeneratorOutput;

	manager.modules.push_back(newModule);

	EvaluatorContext moduleContext = {};
	moduleContext.module = newModule;
	moduleContext.scope = EvaluatorScope_Module;
	moduleContext.definitionName = &manager.globalPseudoInvocationName;
	// Module always requires all its functions
	// TODO: Local functions can be left out if not referenced (in fact, they may warn in C if not)
	moduleContext.isRequired = true;
	// A delimiter isn't strictly necessary here, but it is nice to space out things
	StringOutput moduleDelimiterTemplate = {};
	moduleDelimiterTemplate.modifiers = StringOutMod_NewlineAfter;
	moduleContext.delimiterTemplate = moduleDelimiterTemplate;
	int numErrors =
	    EvaluateGenerateAll_Recursive(manager.environment, moduleContext, *newModule->tokens,
	                                  /*startTokenIndex=*/0, *newModule->generatedOutput);
	// After this point, the module may have references to its tokens in the environmment, so we
	// cannot destroy it until we're done evaluating everything
	if (numErrors)
	{
		Logf("error: failed to evaluate %s\n", newModule->filename);
		return false;
	}

	if (moduleOut)
		*moduleOut = newModule;

	if (logging.imports)
		Logf("Loaded %s\n", newModule->filename);
	return true;
}

bool moduleManagerEvaluateResolveReferences(ModuleManager& manager)
{
	return EvaluateResolveReferences(manager.environment);
}

// Directory is named from build configuration labels, e.g. Debug-HotReload
// Order DOES matter, in case changing configuration order changes which settings get eval'd first
static bool createBuildOutputDirectory(EvaluatorEnvironment& environment, std::string& outputDirOut)
{
	// As soon as we start writing, we need to decide what directory we will write to. Fix build
	// configuration labels because it's too late to change them now
	environment.buildConfigurationLabelsAreFinal = true;

	// Sane default in case something goes wrong
	outputDirOut = cakelispWorkingDir;

	char outputDirName[MAX_PATH_LENGTH] = {0};
	int numLabels = (int)environment.buildConfigurationLabels.size();
	char* writeHead = outputDirName;

	if (!writeStringToBuffer(cakelispWorkingDir, &writeHead, outputDirName, sizeof(outputDirName)))
	{
		Log("error: ran out of space writing build configuration output directory name\n");
		return false;
	}
	if (!writeCharToBuffer('/', &writeHead, outputDirName, sizeof(outputDirName)))
	{
		Log("error: ran out of space writing build configuration output directory name\n");
		return false;
	}

	for (int i = 0; i < numLabels; ++i)
	{
		const std::string& label = environment.buildConfigurationLabels[i];
		if (!writeStringToBuffer(label.c_str(), &writeHead, outputDirName, sizeof(outputDirName)))
		{
			Log("error: ran out of space writing build configuration output directory name\n");
			break;
		}

		// Delimiter
		if (i < numLabels - 1)
		{
			if (!writeCharToBuffer('-', &writeHead, outputDirName, sizeof(outputDirName)))
			{
				Log("error: ran out of space writing build configuration output directory name\n");
				break;
			}
		}
	}

	if (numLabels == 0)
		PrintfBuffer(outputDirName, "%s/default", cakelispWorkingDir);

	makeDirectory(outputDirName);

	if (logging.fileSystem || logging.phases)
		Logf("Outputting artifacts to %s\n", outputDirName);

	outputDirOut = outputDirName;

	return true;
}

bool moduleManagerWriteGeneratedOutput(ModuleManager& manager)
{
	createBuildOutputDirectory(manager.environment, manager.buildOutputDir);

	NameStyleSettings nameSettings;
	WriterFormatSettings formatSettings;

	for (Module* module : manager.modules)
	{
		WriterOutputSettings outputSettings;
		outputSettings.sourceCakelispFilename = module->filename;

		GeneratorOutput header;
		GeneratorOutput footer;
		// Something to attach the reason for generating this output
		const Token* blameToken = &(*module->tokens)[0];
		// Always include my header file
		{
			char relativeIncludeBuffer[MAX_PATH_LENGTH];
			getFilenameFromPath(module->filename, relativeIncludeBuffer,
			                    sizeof(relativeIncludeBuffer));
			// TODO: hpp to h support
			strcat(relativeIncludeBuffer, ".hpp");
			addStringOutput(header.source, "#include", StringOutMod_SpaceAfter, blameToken);
			addStringOutput(header.source, relativeIncludeBuffer, StringOutMod_SurroundWithQuotes,
			                blameToken);
			addLangTokenOutput(header.source, StringOutMod_NewlineAfter, blameToken);
		}
		makeRunTimeHeaderFooter(header, footer, blameToken);
		outputSettings.heading = &header;
		outputSettings.footer = &footer;

		char sourceOutputName[MAX_PATH_LENGTH] = {0};
		if (!outputFilenameFromSourceFilename(manager.buildOutputDir.c_str(),
		                                      outputSettings.sourceCakelispFilename, "cpp",
		                                      sourceOutputName, sizeof(sourceOutputName)))
			return false;
		char headerOutputName[MAX_PATH_LENGTH] = {0};
		if (!outputFilenameFromSourceFilename(manager.buildOutputDir.c_str(),
		                                      outputSettings.sourceCakelispFilename, "hpp",
		                                      headerOutputName, sizeof(headerOutputName)))
			return false;
		module->sourceOutputName = sourceOutputName;
		module->headerOutputName = headerOutputName;
		outputSettings.sourceOutputName = module->sourceOutputName.c_str();
		outputSettings.headerOutputName = module->headerOutputName.c_str();

		if (!writeGeneratorOutput(*module->generatedOutput, nameSettings, formatSettings,
		                          outputSettings))
			return false;
	}

	if (logging.phases || logging.performance)
		Logf("Processed %d lines\n", g_totalLinesTokenized);

	return true;
}

typedef std::unordered_map<std::string, FileModifyTime> HeaderModificationTimeTable;

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

static void OnCompileProcessOutput(const char* output)
{
	// TODO C/C++ error to Cakelisp token mapper
}

// enum BuildTargetType
// {
// 	BuildTargetType_Executable,
// 	BuildTargetType_Library
// };

// struct BuildTarget
// {
// 	BuildTargetType type;
// 	std::vector<std::string> filesToLink;
// };

struct BuiltObject
{
	int buildStatus;
	std::string sourceFilename;
	std::string filename;

	ProcessCommand* buildCommandOverride;
	std::vector<std::string> includesSearchDirs;
	std::vector<std::string> additionalOptions;

	// Only used for include scanning
	std::vector<std::string> headerSearchDirectories;
};

void builtObjectsFree(std::vector<BuiltObject*>& objects)
{
	for (BuiltObject* object : objects)
		delete object;

	objects.clear();
}

void copyModuleBuildOptionsToBuiltObject(Module* module, ProcessCommand* buildCommandOverride,
                                         BuiltObject* object)
{
	object->buildCommandOverride = buildCommandOverride;

	for (const std::string& searchDir : module->cSearchDirectories)
	{
		char searchDirToArgument[MAX_PATH_LENGTH + 2];
		makeIncludeArgument(searchDirToArgument, sizeof(searchDirToArgument), searchDir.c_str());
		object->includesSearchDirs.push_back(searchDirToArgument);
	}

	PushBackAll(object->headerSearchDirectories, module->cSearchDirectories);

	PushBackAll(object->additionalOptions, module->additionalBuildOptions);
}

void getExecutableOutputName(ModuleManager& manager, std::string& finalOutputNameOut)
{
	if (!manager.environment.executableOutput.empty())
		finalOutputNameOut = manager.environment.executableOutput;
	else
		finalOutputNameOut = defaultExecutableName;
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
	return true;
}

// Copy cachedOutputExecutable to finalOutputNameOut, adding executable permissions
// TODO: There's no easy way to know whether this exe is the current build configuration's
// output exe, so copy it every time
bool copyExecutableToFinalOutput(ModuleManager& manager, const std::string& cachedOutputExecutable,
                                 const std::string& finalOutputName)
{
	if (logging.fileSystem)
		Log("Copying executable from cache\n");

	if (!copyBinaryFileTo(cachedOutputExecutable.c_str(), finalOutputName.c_str()))
	{
		Log("error: failed to copy executable from cache\n");
		return false;
	}

// TODO: Consider a better place for this
#ifdef WINDOWS
	char executableLib[MAX_PATH_LENGTH] = {0};
	SafeSnprinf(executableLib, sizeof(executableLib), "%s", cachedOutputExecutable.c_str());

	bool modifiedExtension = changeExtension(executableLib, "lib");

	if (modifiedExtension && fileExists(executableLib))
	{
		char finalOutputLib[MAX_PATH_LENGTH] = {0};
		SafeSnprinf(finalOutputLib, sizeof(finalOutputLib), "%s", finalOutputName.c_str());
		modifiedExtension = changeExtension(finalOutputLib, "lib");

		if (modifiedExtension && !copyBinaryFileTo(executableLib, finalOutputLib))
		{
			Log("error: failed to copy executable lib from cache\n");
			return false;
		}
	}
#endif

	addExecutablePermission(finalOutputName.c_str());
	return true;
}

// commandArguments should have terminating null sentinel
static bool commandEqualsCachedCommand(ModuleManager& manager, const char* artifactKey,
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

	ArtifactCrcTable::iterator findIt = manager.cachedCommandCrcs.find(artifactKey);
	if (findIt == manager.cachedCommandCrcs.end())
	{
		if (logging.commandCrcs)
			Logf("CRC32 for %s: %u (not cached)\n", artifactKey, newCommandCrc);
		return false;
	}

	if (logging.commandCrcs)
		Logf("CRC32 for %s: old %u new %u\n", artifactKey, findIt->second, newCommandCrc);

	return findIt->second == newCommandCrc;
}

static bool moduleManagerReadCacheFile(ModuleManager& manager);
static void moduleManagerWriteCacheFile(ModuleManager& manager);

bool moduleManagerBuild(ModuleManager& manager, std::vector<std::string>& builtOutputs)
{
	if (!moduleManagerReadCacheFile(manager))
		return false;

	int currentNumProcessesSpawned = 0;

	int numModules = manager.modules.size();
	// Pointer because the objects can't move, status codes are pointed to
	std::vector<BuiltObject*> builtObjects;

	for (int moduleIndex = 0; moduleIndex < numModules; ++moduleIndex)
	{
		Module* module = manager.modules[moduleIndex];

		for (ModulePreBuildHook hook : module->preBuildHooks)
		{
			if (!hook(manager, module))
			{
				Log("error: hook returned failure. Aborting build\n");
				builtObjectsFree(builtObjects);
				return false;
			}
		}

		ProcessCommand* buildCommandOverride = nullptr;
		{
			int buildCommandState = 0;
			if (!module->buildTimeBuildCommand.fileToExecute.empty())
				++buildCommandState;
			if (!module->buildTimeBuildCommand.arguments.empty())
				++buildCommandState;
			bool buildCommandValid = buildCommandState == 2;
			if (!buildCommandValid && buildCommandState)
			{
				ErrorAtTokenf(
				    (*module->tokens)[0],
				    "error: module build command override must be completely defined. Missing %s\n",
				    module->buildTimeBuildCommand.fileToExecute.empty() ? "file to execute" :
				                                                          "arguments");
				builtObjectsFree(builtObjects);
				return false;
			}

			if (buildCommandValid)
				buildCommandOverride = &module->buildTimeBuildCommand;
		}

		if (logging.buildProcess)
			Logf("Build module %s\n", module->sourceOutputName.c_str());
		for (ModuleDependency& dependency : module->dependencies)
		{
			if (logging.buildProcess)
				Logf("\tRequires %s\n", dependency.name.c_str());

			// Cakelisp files are built at the module manager level, so we need not concern
			// ourselves with them
			if (dependency.type == ModuleDependency_Cakelisp)
				continue;

			if (dependency.type == ModuleDependency_CFile)
			{
				BuiltObject* newBuiltObject = new BuiltObject;
				newBuiltObject->buildStatus = 0;
				newBuiltObject->sourceFilename = dependency.name;

				char buildObjectName[MAX_PATH_LENGTH] = {0};
				if (!outputFilenameFromSourceFilename(
				        manager.buildOutputDir.c_str(), newBuiltObject->sourceFilename.c_str(),
				        compilerObjectExtension, buildObjectName, sizeof(buildObjectName)))
				{
					delete newBuiltObject;
					Log("error: failed to create suitable output filename");
					builtObjectsFree(builtObjects);
					return false;
				}

				newBuiltObject->filename = buildObjectName;

				// This is a bit weird to automatically use the parent module's build command
				copyModuleBuildOptionsToBuiltObject(module, buildCommandOverride, newBuiltObject);

				builtObjects.push_back(newBuiltObject);
			}
		}

		if (module->skipBuild)
			continue;

		char buildObjectName[MAX_PATH_LENGTH] = {0};
		if (!outputFilenameFromSourceFilename(
		        manager.buildOutputDir.c_str(), module->sourceOutputName.c_str(),
		        compilerObjectExtension, buildObjectName, sizeof(buildObjectName)))
		{
			Log("error: failed to create suitable output filename");
			builtObjectsFree(builtObjects);
			return false;
		}

		// At this point, we do want to build the object. We might skip building it if it is cached.
		// In that case, the status code should still be 0, as if we built and succeeded building it
		BuiltObject* newBuiltObject = new BuiltObject;
		newBuiltObject->buildStatus = 0;
		newBuiltObject->sourceFilename = module->sourceOutputName.c_str();
		newBuiltObject->filename = buildObjectName;

		copyModuleBuildOptionsToBuiltObject(module, buildCommandOverride, newBuiltObject);

		builtObjects.push_back(newBuiltObject);
	}

	HeaderModificationTimeTable headerModifiedCache;

	for (BuiltObject* object : builtObjects)
	{
		std::vector<const char*> searchDirArgs;
		searchDirArgs.reserve(object->includesSearchDirs.size() +
		                      manager.environment.cSearchDirectories.size());
		for (const std::string& searchDirArg : object->includesSearchDirs)
		{
			searchDirArgs.push_back(searchDirArg.c_str());
		}

		// This code sucks
		std::vector<std::string> globalSearchDirArgs;
		globalSearchDirArgs.reserve(manager.environment.cSearchDirectories.size());
		for (const std::string& searchDir : manager.environment.cSearchDirectories)
		{
			char searchDirToArgument[MAX_PATH_LENGTH + 2];
			makeIncludeArgument(searchDirToArgument, sizeof(searchDirToArgument),
			                    searchDir.c_str());
			globalSearchDirArgs.push_back(searchDirToArgument);
			searchDirArgs.push_back(globalSearchDirArgs.back().c_str());
		}

		std::vector<const char*> additionalOptions;
		for (const std::string& option : object->additionalOptions)
		{
			additionalOptions.push_back(option.c_str());
		}

		ProcessCommand& buildCommand = object->buildCommandOverride ?
		                                   *object->buildCommandOverride :
		                                   manager.environment.buildTimeBuildCommand;

		// Annoying exception for MSVC not having spaces between some arguments
		std::string* objectOutput = &object->filename;
		std::string objectOutputOverride;
		if (StrCompareIgnoreCase(buildCommand.fileToExecute.c_str(), "CL.exe") == 0)
		{
			char msvcObjectOutput[MAX_PATH_LENGTH] = {0};
			makeObjectOutputArgument(msvcObjectOutput, sizeof(msvcObjectOutput),
			                         object->filename.c_str());
			objectOutputOverride = msvcObjectOutput;
			objectOutput = &objectOutputOverride;
		}

		ProcessCommandInput buildTimeInputs[] = {
		    {ProcessCommandArgumentType_SourceInput, {object->sourceFilename.c_str()}},
		    {ProcessCommandArgumentType_ObjectOutput, {objectOutput->c_str()}},
		    {ProcessCommandArgumentType_IncludeSearchDirs, std::move(searchDirArgs)},
		    {ProcessCommandArgumentType_AdditionalOptions, std::move(additionalOptions)}};
		const char** buildArguments = MakeProcessArgumentsFromCommand(buildCommand, buildTimeInputs,
		                                                              ArraySize(buildTimeInputs));
		if (!buildArguments)
		{
			Log("error: failed to construct build arguments\n");
			builtObjectsFree(builtObjects);
			return false;
		}

		uint32_t commandCrc = 0;
		bool commandEqualsCached = commandEqualsCachedCommand(manager, object->filename.c_str(),
		                                                      buildArguments, &commandCrc);
		// We could avoid doing this work, but it makes it easier to log if we do it regardless of
		// commandEqualsCached invalidating our cache anyways
		bool canUseCache = canUseCachedFile(manager.environment, object->sourceFilename.c_str(),
		                                    object->filename.c_str());
		bool headersModified = false;
		if (commandEqualsCached && canUseCache)
		{
			std::vector<std::string> headerSearchDirectories;
			{
				headerSearchDirectories.reserve(object->headerSearchDirectories.size() +
				                                manager.environment.cSearchDirectories.size() + 1);
				// Must include CWD to find generated cakelisp files
				headerSearchDirectories.push_back(".");
				PushBackAll(headerSearchDirectories, object->headerSearchDirectories);
				PushBackAll(headerSearchDirectories, manager.environment.cSearchDirectories);
			}

			// Note that I use the .o as "includedBy" because our header may not have needed any
			// changes if our include changed. We have to use the .o as the time reference that
			// we've rebuilt
			FileModifyTime mostRecentHeaderModTime = GetMostRecentIncludeModified_Recursive(
			    headerSearchDirectories, object->sourceFilename.c_str(),
			    /*includedBy*/ nullptr, headerModifiedCache);

			FileModifyTime artifactModTime = fileGetLastModificationTime(object->filename.c_str());
			if (artifactModTime >= mostRecentHeaderModTime)
			{
				if (logging.buildProcess)
					Logf("Skipping compiling %s (using cached object)\n",
					     object->sourceFilename.c_str());
				continue;
			}
			else
			{
				headersModified = true;
				if (logging.includeScanning || logging.buildProcess)
				{
					Logf("--- Must rebuild %s (header files modified)\n",
					     object->sourceFilename.c_str());
					Logf("Artifact: %ul Most recent header: %ul\n", artifactModTime,
					     mostRecentHeaderModTime);
				}
			}
		}

		if (logging.buildReasons)
		{
			Logf("Build %s reason(s):\n", object->filename.c_str());
			if (!canUseCache)
				Log("\tobject files updated\n");
			if (!commandEqualsCached)
				Log("\tcommand changed since last run\n");
			if (headersModified)
				Log("\theaders modified\n");
		}

		if (!commandEqualsCached)
			manager.newCommandCrcs[object->filename] = commandCrc;

		// Go through with the build
		RunProcessArguments compileArguments = {};
		compileArguments.fileToExecute = buildCommand.fileToExecute.c_str();
		compileArguments.arguments = buildArguments;
		// PrintProcessArguments(buildArguments);

		if (runProcess(compileArguments, &object->buildStatus) != 0)
		{
			Log("error: failed to invoke compiler\n");
			free(buildArguments);
			builtObjectsFree(builtObjects);
			return false;
		}

		free(buildArguments);

		// TODO This could be made smarter by allowing more spawning right when a process
		// closes, instead of starting in waves
		++currentNumProcessesSpawned;
		if (currentNumProcessesSpawned >= maxProcessesRecommendedSpawned)
		{
			waitForAllProcessesClosed(OnCompileProcessOutput);
			currentNumProcessesSpawned = 0;
		}
	}

	if (logging.includeScanning || logging.performance)
		Logf("%lu files tested for modification times\n", headerModifiedCache.size());

	waitForAllProcessesClosed(OnCompileProcessOutput);
	currentNumProcessesSpawned = 0;

	std::string outputExecutableName;
	if (!manager.environment.executableOutput.empty())
	{
		char outputExecutableFilename[MAX_PATH_LENGTH] = {0};
		getFilenameFromPath(manager.environment.executableOutput.c_str(), outputExecutableFilename,
		                    sizeof(outputExecutableFilename));

		outputExecutableName = outputExecutableFilename;
	}
	if (outputExecutableName.empty())
		outputExecutableName = defaultExecutableName;

	char outputExecutableCachePath[MAX_PATH_LENGTH] = {0};
	if (!outputFilenameFromSourceFilename(
	        manager.buildOutputDir.c_str(), outputExecutableName.c_str(),
	        /*addExtension=*/nullptr, outputExecutableCachePath, sizeof(outputExecutableCachePath)))
	{
		builtObjectsFree(builtObjects);
		return false;
	}
	outputExecutableName = outputExecutableCachePath;

	int numObjectsToLink = 0;
	bool succeededBuild = true;
	bool objectsDirty = false;
	for (BuiltObject* object : builtObjects)
	{
		// Module* module = manager.modules[moduleIndex];
		int buildResult = object->buildStatus;
		if (buildResult != 0 || !fileExists(object->filename.c_str()))
		{
			Logf("error: failed to make target %s\n", object->filename.c_str());
			succeededBuild = false;
			continue;
		}

		if (logging.buildProcess)
			Logf("Need to link %s\n", object->filename.c_str());

		++numObjectsToLink;

		// If all our objects are older than our executable, don't even link!
		objectsDirty |= !canUseCachedFile(manager.environment, object->filename.c_str(),
		                                  outputExecutableName.c_str());
	}

	if (!succeededBuild)
	{
		builtObjectsFree(builtObjects);
		return false;
	}

	std::string finalOutputName;
	getExecutableOutputName(manager, finalOutputName);

	if (numObjectsToLink)
	{
		std::vector<const char*> objectsToLink(numObjectsToLink);
		for (int i = 0; i < numObjectsToLink; ++i)
		{
			BuiltObject* object = builtObjects[i];

			objectsToLink[i] = object->filename.c_str();
		}

		ProcessCommand linkCommand = manager.environment.buildTimeLinkCommand;

		// Annoying exception for MSVC not having spaces between some arguments
		std::string* executableOutput = &outputExecutableName;
		std::string executableOutputOverride;
		if (StrCompareIgnoreCase(linkCommand.fileToExecute.c_str(), "cl.exe") == 0)
		{
			char msvcExecutableOutput[MAX_PATH_LENGTH] = {0};
			PrintfBuffer(msvcExecutableOutput, "/Fe\"%s\"", outputExecutableName.c_str());
			executableOutputOverride = msvcExecutableOutput;
			executableOutput = &executableOutputOverride;
		}
		else if (StrCompareIgnoreCase(linkCommand.fileToExecute.c_str(), "link.exe") == 0)
		{
			char msvcExecutableOutput[MAX_PATH_LENGTH] = {0};
			PrintfBuffer(msvcExecutableOutput, "/out:\"%s\"", outputExecutableName.c_str());
			executableOutputOverride = msvcExecutableOutput;
			executableOutput = &executableOutputOverride;
		}

		// Copy it so hooks can modify it
		ProcessCommandInput linkTimeInputs[] = {
		    {ProcessCommandArgumentType_ExecutableOutput, {executableOutput->c_str()}},
		    {ProcessCommandArgumentType_ObjectInput, objectsToLink}};

		// Hooks should cooperate with eachother, i.e. try to only add things
		for (PreLinkHook preLinkHook : manager.environment.preLinkHooks)
		{
			if (!preLinkHook(manager, linkCommand, linkTimeInputs, ArraySize(linkTimeInputs)))
			{
				Log("error: hook returned failure. Aborting build\n");
				builtObjectsFree(builtObjects);
				return false;
			}
		}

		const char** linkArgumentList =
		    MakeProcessArgumentsFromCommand(linkCommand, linkTimeInputs, ArraySize(linkTimeInputs));
		if (!linkArgumentList)
		{
			builtObjectsFree(builtObjects);
			return false;
		}

		uint32_t commandCrc = 0;
		bool commandEqualsCached = commandEqualsCachedCommand(manager, finalOutputName.c_str(),
		                                                      linkArgumentList, &commandCrc);

		// Check if we can use the cached version
		if (!objectsDirty && commandEqualsCached)
		{
			if (logging.buildProcess)
				Log("Skipping linking (no built objects are newer than cached executable, command "
				    "identical)\n");

			{
				if (!copyExecutableToFinalOutput(manager, outputExecutableName, finalOutputName))
				{
					free(linkArgumentList);
					builtObjectsFree(builtObjects);
					return false;
				}

				Logf("No changes needed for %s\n", finalOutputName.c_str());
				builtOutputs.push_back(finalOutputName);
			}

			free(linkArgumentList);
			builtObjectsFree(builtObjects);
			moduleManagerWriteCacheFile(manager);
			return true;
		}

		if (logging.buildReasons)
		{
			Logf("Link %s reason(s):\n", finalOutputName.c_str());
			if (objectsDirty)
				Log("\tobject files updated\n");
			if (!commandEqualsCached)
				Log("\tcommand changed since last run\n");
		}

		if (!commandEqualsCached)
			manager.newCommandCrcs[finalOutputName] = commandCrc;

		RunProcessArguments linkArguments = {};
		linkArguments.fileToExecute = linkCommand.fileToExecute.c_str();
		linkArguments.arguments = linkArgumentList;
		int linkStatus = 0;
		if (runProcess(linkArguments, &linkStatus) != 0)
		{
			free(linkArgumentList);
			builtObjectsFree(builtObjects);
			return false;
		}

		free(linkArgumentList);

		waitForAllProcessesClosed(OnCompileProcessOutput);

		succeededBuild = linkStatus == 0;
	}

	if (!succeededBuild)
	{
		builtObjectsFree(builtObjects);
		return false;
	}

	{
		if (!copyExecutableToFinalOutput(manager, outputExecutableName, finalOutputName))
		{
			builtObjectsFree(builtObjects);
			return false;
		}

		Logf("Successfully built and linked %s\n", finalOutputName.c_str());
		builtOutputs.push_back(finalOutputName);
	}

	builtObjectsFree(builtObjects);
	moduleManagerWriteCacheFile(manager);
	return true;
}

// Returns false if there were errors; the file not existing is not an error
static bool moduleManagerReadCacheFile(ModuleManager& manager)
{
	char inputFilename[MAX_PATH_LENGTH] = {0};
	if (!outputFilenameFromSourceFilename(manager.buildOutputDir.c_str(), "Cache", "cake",
	                                      inputFilename, sizeof(inputFilename)))
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
				manager.cachedCommandCrcs[(*tokens)[artifactIndex].contents] =
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

static void moduleManagerWriteCacheFile(ModuleManager& manager)
{
	char outputFilename[MAX_PATH_LENGTH] = {0};
	if (!outputFilenameFromSourceFilename(manager.buildOutputDir.c_str(), "Cache", "cake",
	                                      outputFilename, sizeof(outputFilename)))
	{
		Log("error: failed to create cache file name\n");
		return;
	}

	// Combine all CRCs into a single map
	ArtifactCrcTable outputCrcs;
	for (ArtifactCrcTablePair& crcPair : manager.cachedCommandCrcs)
		outputCrcs.insert(crcPair);
	// New commands override previously cached
	for (ArtifactCrcTablePair& crcPair : manager.newCommandCrcs)
		outputCrcs[crcPair.first] = crcPair.second;

	std::vector<Token> outputTokens;
	const Token openParen = {TokenType_OpenParen, EmptyString, "ModuleManager.cpp", 1, 0, 0};
	const Token closeParen = {TokenType_CloseParen, EmptyString, "ModuleManager.cpp", 1, 0, 0};
	const Token crcInvoke = {TokenType_Symbol, "command-crc", "ModuleManager.cpp", 1, 0, 0};

	for (ArtifactCrcTablePair& crcPair : outputCrcs)
	{
		outputTokens.push_back(openParen);
		outputTokens.push_back(crcInvoke);

		Token artifactName = {TokenType_String, crcPair.first, "ModuleManager.cpp", 1, 0, 0};
		outputTokens.push_back(artifactName);

		Token crcToken = {
		    TokenType_Symbol, std::to_string(crcPair.second), "ModuleManager.cpp", 1, 0, 0};
		outputTokens.push_back(crcToken);

		outputTokens.push_back(closeParen);
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
