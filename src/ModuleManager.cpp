#include "ModuleManager.hpp"

#include "Building.hpp"
#include "Converters.hpp"
#include "DynamicLoader.hpp"
#include "Evaluator.hpp"
#include "EvaluatorEnums.hpp"
#include "FileUtilities.hpp"
#include "Generators.hpp"
#include "Logging.hpp"
#include "OutputPreambles.hpp"
#include "RunProcess.hpp"
#include "Tokenizer.hpp"
#include "Utilities.hpp"
#include "Writer.hpp"

#include <string.h>

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
		moduleDefinition.name = &manager.globalPseudoInvocationName;
		moduleDefinition.type = ObjectType_PseudoObject;
		moduleDefinition.isRequired = true;
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
		    {ProcessCommandArgumentType_String, "-fPIC"}};

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

bool moduleManagerAddEvaluateFile(ModuleManager& manager, const char* filename)
{
	for (Module* module : manager.modules)
	{
		if (strcmp(module->filename, filename) == 0)
		{
			printf("Already loaded %s\n", filename);
			return true;
		}
	}

	Module* newModule = new Module();
	// We need to keep this memory around for the lifetime of the token, regardless of relocation
	newModule->filename = strdup(filename);
	// This stage cleans up after itself if it fails
	if (!moduleLoadTokenizeValidate(newModule->filename, &newModule->tokens))
		return false;

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
	StringOutput bodyDelimiterTemplate = {};
	bodyDelimiterTemplate.modifiers = StringOutMod_NewlineAfter;
	int numErrors = EvaluateGenerateAll_Recursive(
	    manager.environment, moduleContext, *newModule->tokens,
	    /*startTokenIndex=*/0, &bodyDelimiterTemplate, *newModule->generatedOutput);
	// After this point, the module may have references to its tokens in the environmment, so we
	// cannot destroy it until we're done evaluating everything
	if (numErrors)
		return false;

	printf("Loaded %s\n", newModule->filename);
	return true;
}

bool moduleManagerEvaluateResolveReferences(ModuleManager& manager)
{
	return EvaluateResolveReferences(manager.environment);
}

bool moduleManagerWriteGeneratedOutput(ModuleManager& manager)
{
	NameStyleSettings nameSettings;
	WriterFormatSettings formatSettings;

	for (Module* module : manager.modules)
	{
		WriterOutputSettings outputSettings;
		outputSettings.sourceCakelispFilename = module->filename;

		// TODO: hpp to h support
		char relativeIncludeBuffer[MAX_PATH_LENGTH];
		getFilenameFromPath(module->filename, relativeIncludeBuffer, sizeof(relativeIncludeBuffer));
		char sourceHeadingBuffer[1024] = {0};
		PrintfBuffer(sourceHeadingBuffer, "#include \"%s.hpp\"\n%s", relativeIncludeBuffer,
		             generatedSourceHeading ? generatedSourceHeading : "");
		outputSettings.sourceHeading = sourceHeadingBuffer;
		outputSettings.sourceFooter = generatedSourceFooter;
		outputSettings.headerHeading = generatedHeaderHeading;
		outputSettings.headerFooter = generatedHeaderFooter;

		char sourceOutputName[MAX_PATH_LENGTH] = {0};
		PrintfBuffer(sourceOutputName, "%s.cpp", outputSettings.sourceCakelispFilename);
		char headerOutputName[MAX_PATH_LENGTH] = {0};
		PrintfBuffer(headerOutputName, "%s.hpp", outputSettings.sourceCakelispFilename);
		module->sourceOutputName = sourceOutputName;
		module->headerOutputName = headerOutputName;
		outputSettings.sourceOutputName = module->sourceOutputName.c_str();
		outputSettings.headerOutputName = module->headerOutputName.c_str();

		if (!writeGeneratorOutput(*module->generatedOutput, nameSettings, formatSettings,
		                          outputSettings))
			return false;
	}

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
};

void builtObjectsFree(std::vector<BuiltObject*>& objects)
{
	for (BuiltObject* object : objects)
		delete object;

	objects.clear();
}

bool moduleManagerBuild(ModuleManager& manager)
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

		ProcessCommand* buildCommandOverride =
		    (!module->buildTimeBuildCommand.fileToExecute.empty() &&
		     !module->buildTimeBuildCommand.arguments.empty()) ?
		        &module->buildTimeBuildCommand :
		        nullptr;

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
				// TODO: Better to use search directories
				char sourceName[MAX_PATH_LENGTH] = {0};
				makePathRelativeToFile(module->sourceOutputName.c_str(), dependency.name.c_str(),
				                       sourceName, sizeof(sourceName));
				newBuiltObject->sourceFilename = sourceName;

				char buildObjectName[MAX_PATH_LENGTH] = {0};
				if (!objectFilenameFromSourceFilename(cakelispWorkingDir,
				                                      newBuiltObject->sourceFilename.c_str(),
				                                      buildObjectName, sizeof(buildObjectName)))
				{
					builtObjectsFree(builtObjects);
					return false;
				}

				newBuiltObject->filename = buildObjectName;
				// TODO: This is a bit weird to automatically use the parent module's build command
				newBuiltObject->buildCommandOverride = buildCommandOverride;
				builtObjects.push_back(newBuiltObject);
			}
		}

		if (module->skipBuild)
			continue;

		char buildObjectName[MAX_PATH_LENGTH] = {0};
		if (!objectFilenameFromSourceFilename(cakelispWorkingDir, module->sourceOutputName.c_str(),
		                                      buildObjectName, sizeof(buildObjectName)))
		{
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
			ProcessCommand& buildCommand = object->buildCommandOverride ?
			                                   *object->buildCommandOverride :
			                                   manager.environment.buildTimeBuildCommand;

			ProcessCommandInput buildTimeInputs[] = {
			    {ProcessCommandArgumentType_SourceInput, {object->sourceFilename.c_str()}},
			    {ProcessCommandArgumentType_ObjectOutput, {object->filename.c_str()}}};
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

	std::string outputExecutableName = manager.environment.executableOutput;
	if (outputExecutableName.empty())
		outputExecutableName = "a.out";

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
		needsLink |=
		    !canUseCachedFile(manager.environment, object->filename.c_str(), outputExecutableName.c_str());
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

		printf("Successfully built and linked %s (no changes)\n", outputExecutableName.c_str());

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

	printf("Successfully built and linked %s\n", outputExecutableName.c_str());

	builtObjectsFree(builtObjects);
	return true;
}
