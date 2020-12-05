#include "ModuleManager.hpp"

#include <string.h>

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

const char* compilerObjectExtension = "o";

// The ' symbols tell the signature validator that the actual contents of those symbols can be
// user-defined (just like C letting you specify arguments without names)
const char* g_modulePreBuildHookSignature =
    "('manager (& ModuleManager) 'module (* Module) &return bool)";

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
			printf(
			    "error: <global> couldn't be added. Was module manager initialized twice? Things "
			    "will definitely break\n");
	}

	manager.environment.moduleManager = &manager;

	// Command defaults
	{
		manager.environment.compileTimeBuildCommand.fileToExecute = "/usr/bin/clang++";
		manager.environment.compileTimeBuildCommand.arguments = {
		    {ProcessCommandArgumentType_String, "-g"},
		    {ProcessCommandArgumentType_String, "-c"},
		    {ProcessCommandArgumentType_SourceInput, EmptyString},
		    {ProcessCommandArgumentType_String, "-o"},
		    {ProcessCommandArgumentType_ObjectOutput, EmptyString},
		    {ProcessCommandArgumentType_CakelispHeadersInclude, EmptyString},
		    {ProcessCommandArgumentType_String, "-fPIC"}};

		manager.environment.compileTimeLinkCommand.fileToExecute = "/usr/bin/clang++";
		manager.environment.compileTimeLinkCommand.arguments = {
		    {ProcessCommandArgumentType_String, "-shared"},
		    {ProcessCommandArgumentType_String, "-o"},
		    {ProcessCommandArgumentType_DynamicLibraryOutput, EmptyString},
		    {ProcessCommandArgumentType_ObjectInput, EmptyString}};

		manager.environment.buildTimeBuildCommand.fileToExecute = "/usr/bin/clang++";
		manager.environment.buildTimeBuildCommand.arguments = {
		    {ProcessCommandArgumentType_String, "-g"},
		    {ProcessCommandArgumentType_String, "-c"},
		    {ProcessCommandArgumentType_SourceInput, EmptyString},
		    {ProcessCommandArgumentType_String, "-o"},
		    {ProcessCommandArgumentType_ObjectOutput, EmptyString},
		    {ProcessCommandArgumentType_String, "-fPIC"},
		    {ProcessCommandArgumentType_IncludeSearchDirs, EmptyString},
		    {ProcessCommandArgumentType_AdditionalOptions, EmptyString}};

		manager.environment.buildTimeLinkCommand.fileToExecute = "/usr/bin/clang++";
		manager.environment.buildTimeLinkCommand.arguments = {
		    {ProcessCommandArgumentType_String, "-o"},
		    {ProcessCommandArgumentType_ExecutableOutput, EmptyString},
		    {ProcessCommandArgumentType_ObjectInput, EmptyString}};

		// TODO: Add defaults for Windows
#ifdef WINDOWS
#error Set sensible defaults for compile time build command
#endif
	}

	manager.environment.useCachedFiles = true;
	makeDirectory(cakelispWorkingDir);
	printf("Using cache at %s\n", cakelispWorkingDir);

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
		while (fgets(lineBuffer, sizeof(lineBuffer), file))
		{
			if (log.tokenization)
				printf("%s", lineBuffer);

			const char* error =
			    tokenizeLine(lineBuffer, filename, lineNumber, *tokens_CREATIONONLY);
			if (error != nullptr)
			{
				printf("%s:%d: error: %s\n", filename, lineNumber, error);

				delete tokens_CREATIONONLY;
				return false;
			}

			lineNumber++;
		}

		// Make it const to avoid pointer invalidation due to resize
		tokens = tokens_CREATIONONLY;
	}

	if (log.tokenization)
		printf("Tokenized %d lines\n", lineNumber - 1);

	if (tokens->empty())
	{
		printf("error: empty file. Please remove from system, or add (ignore)\n");
		delete tokens;
		return false;
	}

	if (!validateParentheses(*tokens))
	{
		delete tokens;
		return false;
	}

	if (log.tokenization)
	{
		printf("\nResult:\n");

		// No need to validate, we already know it's safe
		int nestingDepth = 0;
		for (const Token& token : *tokens)
		{
			printIndentToDepth(nestingDepth);

			printf("%s", tokenTypeToString(token.type));

			bool printRanges = true;
			if (printRanges)
			{
				printf("\t\tline %d, from line character %d to %d\n", token.lineNumber,
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
				printf("\t%s\n", token.contents.c_str());
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
	const char* normalizedFilename = strdup(resolvedPath);
	// Enabling this makes all file:line messages really long. For now, I'll keep it as relative to
	// current working directory of this executable.
	// const char* normalizedFilename = makeAbsolutePath_Allocated(".", filename);
	if (!normalizedFilename)
	{
#ifdef UNIX
		perror("failed to normalize filename: ");
#else
		printf("error: could not normalize filename, or file not found: %s\n", filename);
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
			printf("error: failed to normalize path %s", filename);
			free((void*)normalizedFilename);
			return false;
		}

		const char* normalizedModuleFilename = makeAbsolutePath_Allocated(".", module->filename);

		if (!normalizedModuleFilename)
		{
			printf("error: failed to normalize path %s", module->filename);
			free((void*)normalizedProspectiveModuleFilename);
			free((void*)normalizedFilename);
			return false;
		}

		if (strcmp(normalizedModuleFilename, normalizedProspectiveModuleFilename) == 0)
		{
			if (moduleOut)
				*moduleOut = module;

			printf("Already loaded %s\n", normalizedFilename);
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
	int numErrors = EvaluateGenerateAll_Recursive(
	    manager.environment, moduleContext, *newModule->tokens,
	    /*startTokenIndex=*/0, &moduleDelimiterTemplate, *newModule->generatedOutput);
	// After this point, the module may have references to its tokens in the environmment, so we
	// cannot destroy it until we're done evaluating everything
	if (numErrors)
		return false;

	if (moduleOut)
		*moduleOut = newModule;

	printf("Loaded %s\n", newModule->filename);
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
		printf("error: ran out of space writing build configuration output directory name\n");
		return false;
	}
	if (!writeCharToBuffer('/', &writeHead, outputDirName, sizeof(outputDirName)))
	{
		printf("error: ran out of space writing build configuration output directory name\n");
		return false;
	}

	for (int i = 0; i < numLabels; ++i)
	{
		const std::string& label = environment.buildConfigurationLabels[i];
		if (!writeStringToBuffer(label.c_str(), &writeHead, outputDirName, sizeof(outputDirName)))
		{
			printf("error: ran out of space writing build configuration output directory name\n");
			break;
		}

		// Delimiter
		if (i < numLabels - 1)
		{
			if (!writeCharToBuffer('-', &writeHead, outputDirName, sizeof(outputDirName)))
			{
				printf(
				    "error: ran out of space writing build configuration output directory name\n");
				break;
			}
		}
	}

	if (numLabels == 0)
		PrintfBuffer(outputDirName, "%s/default", cakelispWorkingDir);

	makeDirectory(outputDirName);

	printf("Outputting artifacts to %s\n", outputDirName);

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

	printf("Processed %d lines\n", g_totalLinesTokenized);

	return true;
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
};

void builtObjectsFree(std::vector<BuiltObject*>& objects)
{
	for (BuiltObject* object : objects)
		delete object;

	objects.clear();
}

bool moduleManagerBuild(ModuleManager& manager, std::vector<std::string>& builtOutputs)
{
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
				printf("error: hook returned failure. Aborting build\n");
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

		if (log.buildProcess)
			printf("Build module %s\n", module->sourceOutputName.c_str());
		for (ModuleDependency& dependency : module->dependencies)
		{
			if (log.buildProcess)
				printf("\tRequires %s\n", dependency.name.c_str());

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
					printf("error: failed to create suitable output filename");
					builtObjectsFree(builtObjects);
					return false;
				}

				newBuiltObject->filename = buildObjectName;
				// TODO: This is a bit weird to automatically use the parent module's build command
				newBuiltObject->buildCommandOverride = buildCommandOverride;

				for (const std::string& searchDir : module->cSearchDirectories)
				{
					char searchDirToArgument[MAX_PATH_LENGTH + 2];
					PrintfBuffer(searchDirToArgument, "-I%s", searchDir.c_str());
					newBuiltObject->includesSearchDirs.push_back(searchDirToArgument);
				}
				PushBackAll(newBuiltObject->additionalOptions, module->additionalBuildOptions);

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
			printf("error: failed to create suitable output filename");
			builtObjectsFree(builtObjects);
			return false;
		}

		// At this point, we do want to build the object. We might skip building it if it is cached.
		// In that case, the status code should still be 0, as if we built and succeeded building it
		BuiltObject* newBuiltObject = new BuiltObject;
		newBuiltObject->buildStatus = 0;
		newBuiltObject->sourceFilename = module->sourceOutputName.c_str();
		newBuiltObject->filename = buildObjectName;
		newBuiltObject->buildCommandOverride = buildCommandOverride;

		for (const std::string& searchDir : module->cSearchDirectories)
		{
			char searchDirToArgument[MAX_PATH_LENGTH + 2];
			PrintfBuffer(searchDirToArgument, "-I%s", searchDir.c_str());
			newBuiltObject->includesSearchDirs.push_back(searchDirToArgument);
		}

		PushBackAll(newBuiltObject->additionalOptions, module->additionalBuildOptions);

		builtObjects.push_back(newBuiltObject);
	}

	for (BuiltObject* object : builtObjects)
	{
		if (canUseCachedFile(manager.environment, object->sourceFilename.c_str(),
		                     object->filename.c_str()))
		{
			if (log.buildProcess)
				printf("Skipping compiling %s (using cached object)\n",
				       object->sourceFilename.c_str());
		}
		else
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
				PrintfBuffer(searchDirToArgument, "-I%s", searchDir.c_str());
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

			ProcessCommandInput buildTimeInputs[] = {
			    {ProcessCommandArgumentType_SourceInput, {object->sourceFilename.c_str()}},
			    {ProcessCommandArgumentType_ObjectOutput, {object->filename.c_str()}},
			    {ProcessCommandArgumentType_IncludeSearchDirs, std::move(searchDirArgs)},
			    {ProcessCommandArgumentType_AdditionalOptions, std::move(additionalOptions)}};
			const char** buildArguments = MakeProcessArgumentsFromCommand(
			    buildCommand, buildTimeInputs, ArraySize(buildTimeInputs));
			if (!buildArguments)
			{
				printf("error: failed to construct build arguments\n");
				builtObjectsFree(builtObjects);
				return false;
			}
			RunProcessArguments compileArguments = {};
			compileArguments.fileToExecute = buildCommand.fileToExecute.c_str();
			compileArguments.arguments = buildArguments;
			// PrintProcessArguments(buildArguments);

			if (runProcess(compileArguments, &object->buildStatus) != 0)
			{
				printf("error: failed to invoke compiler\n");
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
	}

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
		outputExecutableName = "a.out";

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
	bool needsLink = false;
	for (BuiltObject* object : builtObjects)
	{
		// Module* module = manager.modules[moduleIndex];
		int buildResult = object->buildStatus;
		if (buildResult != 0)
		{
			printf("error: failed to make target %s\n", object->filename.c_str());
			succeededBuild = false;
			continue;
		}

		if (log.buildProcess)
			printf("Need to link %s\n", object->filename.c_str());

		++numObjectsToLink;

		// If all our objects are older than our executable, don't even link!
		needsLink |= !canUseCachedFile(manager.environment, object->filename.c_str(),
		                               outputExecutableName.c_str());
	}

	if (!succeededBuild)
	{
		builtObjectsFree(builtObjects);
		return false;
	}

	if (!needsLink)
	{
		if (log.buildProcess)
			printf("Skipping linking (no built objects are newer than cached executable)\n");

		// TODO: There's no easy way to know whether this exe is the current build configuration's
		// output exe, so copy it every time
		{
			std::string finalOutputName;
			printf("Copying executable from cache\n");
			if (!manager.environment.executableOutput.empty())
				finalOutputName = manager.environment.executableOutput;
			else
				finalOutputName = "a.out";

			if (!copyBinaryFileTo(outputExecutableName.c_str(), finalOutputName.c_str()))
			{
				builtObjectsFree(builtObjects);
				return false;
			}

			printf("Successfully built and linked %s\n", finalOutputName.c_str());
			builtOutputs.push_back(finalOutputName);
		}

		builtObjectsFree(builtObjects);
		return true;
	}

	if (numObjectsToLink)
	{
		std::vector<const char*> objectsToLink(numObjectsToLink);
		for (int i = 0; i < numObjectsToLink; ++i)
		{
			BuiltObject* object = builtObjects[i];

			objectsToLink[i] = object->filename.c_str();
		}

		// Copy it so hooks can modify it
		ProcessCommand linkCommand = manager.environment.buildTimeLinkCommand;
		ProcessCommandInput linkTimeInputs[] = {
		    {ProcessCommandArgumentType_ExecutableOutput, {outputExecutableName.c_str()}},
		    {ProcessCommandArgumentType_ObjectInput, objectsToLink}};

		// Hooks should cooperate with eachother, i.e. try to only add things
		for (PreLinkHook preLinkHook : manager.environment.preLinkHooks)
		{
			if (!preLinkHook(manager, linkCommand, linkTimeInputs, ArraySize(linkTimeInputs)))
			{
				printf("error: hook returned failure. Aborting build\n");
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
		RunProcessArguments linkArguments = {};
		linkArguments.fileToExecute = linkCommand.fileToExecute.c_str();
		linkArguments.arguments = linkArgumentList;
		int linkStatus = 0;
		if (runProcess(linkArguments, &linkStatus) != 0)
		{
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

	// TODO: There's no easy way to know whether this exe is the current build configuration's
	// output exe, so copy it every time
	{
		std::string finalOutputName;
		printf("Copying executable from cache\n");
		if (!manager.environment.executableOutput.empty())
			finalOutputName = manager.environment.executableOutput;
		else
			finalOutputName = "a.out";

		if (!copyBinaryFileTo(outputExecutableName.c_str(), finalOutputName.c_str()))
		{
			builtObjectsFree(builtObjects);
			return false;
		}

		printf("Successfully built and linked %s\n", finalOutputName.c_str());
		builtOutputs.push_back(finalOutputName);
	}

	builtObjectsFree(builtObjects);
	return true;
}
