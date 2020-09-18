#include "ModuleManager.hpp"

#include "Converters.hpp"
#include "Evaluator.hpp"
#include "FileUtilities.hpp"
#include "Generators.hpp"
#include "OutputPreambles.hpp"
#include "RunProcess.hpp"
#include "Tokenizer.hpp"
#include "Utilities.hpp"
#include "Writer.hpp"

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

	makeDirectory(cakelispWorkingDir);
	printf("Using cache at %s\n", cakelispWorkingDir);
}

void moduleManagerDestroy(ModuleManager& manager)
{
	environmentDestroyInvalidateTokens(manager.environment);
	for (Module& module : manager.modules)
	{
		delete module.tokens;
		delete module.generatedOutput;
	}
	manager.modules.clear();
}

bool moduleLoadTokenizeValidate(const char* filename, const std::vector<Token>** tokensOut)
{
	bool verbose = false;

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
			if (verbose)
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

	if (verbose)
		printf("Tokenized %d lines\n", lineNumber - 1);

	if (!validateParentheses(*tokens))
	{
		delete tokens;
		return false;
	}

	if (verbose)
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
	for (Module& module : manager.modules)
	{
		if (module.filename.compare(filename) == 0)
		{
			printf("Already loaded %s\n", filename);
			return true;
		}
	}

	// TODO: Add relative path support
	Module newModule = {};
	newModule.filename = filename;
	// This stage cleans up after itself if it fails
	if (!moduleLoadTokenizeValidate(filename, &newModule.tokens))
		return false;

	newModule.generatedOutput = new GeneratorOutput;

	manager.modules.push_back(newModule);

	EvaluatorContext moduleContext = {};
	moduleContext.scope = EvaluatorScope_Module;
	moduleContext.definitionName = &manager.globalPseudoInvocationName;
	// Module always requires all its functions
	// TODO: Local functions can be left out if not referenced (in fact, they may warn in C if not)
	moduleContext.isRequired = true;
	// A delimiter isn't strictly necessary here, but it is nice to space out things
	StringOutput bodyDelimiterTemplate = {};
	bodyDelimiterTemplate.modifiers = StringOutMod_NewlineAfter;
	int numErrors = EvaluateGenerateAll_Recursive(
	    manager.environment, moduleContext, *newModule.tokens,
	    /*startTokenIndex=*/0, &bodyDelimiterTemplate, *newModule.generatedOutput);
	// After this point, the module may have references to its tokens in the environmment, so we
	// cannot destroy it until we're done evaluating everything
	if (numErrors)
		return false;

	printf("Loaded %s\n", filename);
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

	for (Module& module : manager.modules)
	{
		WriterOutputSettings outputSettings;
		outputSettings.sourceCakelispFilename = module.filename.c_str();

		char sourceHeadingBuffer[1024] = {0};
		// TODO: hpp to h support
		// TODO: Strip path from filename
		PrintfBuffer(sourceHeadingBuffer, "#include \"%s.hpp\"\n%s", module.filename.c_str(),
		             generatedSourceHeading ? generatedSourceHeading : "");
		outputSettings.sourceHeading = sourceHeadingBuffer;
		outputSettings.sourceFooter = generatedSourceFooter;
		outputSettings.headerHeading = generatedHeaderHeading;
		outputSettings.headerFooter = generatedHeaderFooter;

		if (!writeGeneratorOutput(*module.generatedOutput, nameSettings, formatSettings, outputSettings))
			return false;
	}

	return true;
}
