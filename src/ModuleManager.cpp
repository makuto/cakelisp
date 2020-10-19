#include "ModuleManager.hpp"

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
		moduleDefinition.type = ObjectType_Function;
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

		manager.environment.buildTimeBuildCommand.fileToExecute = "/usr/bin/clang++";
		manager.environment.buildTimeBuildCommand.arguments = {
		    {ProcessCommandArgumentType_String, "-g"},
		    {ProcessCommandArgumentType_String, "-c"},
		    {ProcessCommandArgumentType_SourceInput, EmptyString},
		    {ProcessCommandArgumentType_String, "-o"},
		    {ProcessCommandArgumentType_ObjectOutput, EmptyString},
		    {ProcessCommandArgumentType_String, "-fPIC"}};

		manager.environment.buildTimeLinkCommand.fileToExecute = "/usr/bin/clang++";
	}

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

bool moduleManagerBuild(ModuleManager& manager)
{
	for (Module* module : manager.modules)
	{
		printf("Build module %s\n", module->sourceOutputName.c_str());
		for (ModuleDependency& dependency : module->dependencies)
		{
			printf("\tRequires %s\n", dependency.name.c_str());

			// Cakelisp files are built at the module manager level, so we need not concern
			// ourselves with them
			if (dependency.type == ModuleDependency_Cakelisp)
				continue;
		}
	}

	return true;
}
