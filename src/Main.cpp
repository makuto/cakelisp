#include <stdio.h>
#include <string.h>

#include <vector>

#include "Converters.hpp"
#include "Evaluator.hpp"
#include "Generators.hpp"
#include "RunProcess.hpp"
#include "Tokenizer.hpp"
#include "Utilities.hpp"
#include "Writer.hpp"

int main(int argc, char* argv[])
{
	if (argc != 2)
	{
		printf("Need to provide a file to parse\n");
		return 1;
	}

	printf("\nTokenization:\n");

	FILE* file = nullptr;
	const char* filename = argv[1];
	file = fopen(filename, "r");
	if (!file)
	{
		printf("Error: Could not open %s\n", filename);
		return 1;
	}
	else
	{
		printf("Opened %s\n", filename);
	}

	bool verbose = false;

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
				return 1;
			}

			lineNumber++;
		}

		// Make it const to avoid pointer invalidation due to resize
		tokens = tokens_CREATIONONLY;
	}

	printf("Tokenized %d lines\n", lineNumber - 1);

	if (!validateParentheses(*tokens))
	{
		delete tokens;
		return 1;
	}

	bool printTokenizerOutput = false;
	if (printTokenizerOutput)
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

	// TODO Move
	// if (false)
	// {
	// 	char sourceOutputName[MAX_PATH_LENGTH] = {0};
	// 	PrintfBuffer(sourceOutputName, "%s.cpp", filename);
	// 	char fileToExec[MAX_PATH_LENGTH] = {0};
	// 	PrintBuffer(fileToExec, "/usr/bin/clang++");
	// 	// PrintBuffer(arguments.fileToExecute, "/usr/bin/ls");

	// 	// char arg0[64] = {0};
	// 	// PrintBuffer(arg0, "--version");

	// 	// If not null terminated, the call will fail
	// 	// char* arguments[] = {arguments.fileToExecute, strdup("--version"), nullptr};
	// 	char* arguments[] = {fileToExec, strdup("-c"), sourceOutputName, nullptr};
	// 	CompilationArguments compilationArguments = {};
	// 	compilationArguments.fileToExecute = fileToExec;
	// 	compilationArguments.arguments = arguments;
	// 	int status = -1;
	// 	if (compileFile(compilationArguments, &status) != 0)
	// 	{
	// 		delete tokens;
	// 		return 1;
	// 	}
	// 	// wa
	// }

	printf("\nParsing and code generation:\n");

	EvaluatorEnvironment environment = {};
	importFundamentalGenerators(environment);
	// TODO Remove test macro
	environment.macros["square"] = SquareMacro;
	EvaluatorContext moduleContext = {};
	moduleContext.scope = EvaluatorScope_Module;
	// Module always requires all its functions
	// TODO: Local functions can be left out if not referenced (in fact, they may warn in C if not)
	moduleContext.isRequired = true;
	GeneratorOutput generatedOutput;
	StringOutput bodyDelimiterTemplate = {"", StringOutMod_NewlineAfter, nullptr, nullptr};
	int numErrors = EvaluateGenerateAll_Recursive(environment, moduleContext, *tokens,
	                                              /*startTokenIndex=*/0, bodyDelimiterTemplate,
	                                              generatedOutput);
	if (numErrors)
	{
		environmentDestroyInvalidateTokens(environment);
		delete tokens;
		return 1;
	}

	if (!EvaluateResolveReferences(environment))
	{
		// TODO Add compile time too
		// if (!environment.unknownReferences.empty() ||
		//     !environment.unknownReferencesForCompileTime.empty())
		// {
		// 	for (const UnknownReference& reference : environment.unknownReferences)
		// 	{
		// 		ErrorAtTokenf(*reference.symbolReference, "reference to undefined symbol '%s'",
		// 		              reference.symbolReference->contents.c_str());
		// 	}

		// 	for (const UnknownReference& reference : environment.unknownReferencesForCompileTime)
		// 	{
		// 		ErrorAtTokenf(*reference.symbolReference, "reference to undefined symbol '%s'",
		// 		              reference.symbolReference->contents.c_str());
		// 	}

			environmentDestroyInvalidateTokens(environment);
			delete tokens;
			return 1;
		// }
	}

	{
		NameStyleSettings nameSettings;
		WriterFormatSettings formatSettings;
		WriterOutputSettings outputSettings;
		outputSettings.sourceCakelispFilename = filename;

		printf("\nResult:\n");

		if (!writeGeneratorOutput(generatedOutput, nameSettings, formatSettings, outputSettings))
		{
			environmentDestroyInvalidateTokens(environment);
			delete tokens;
			return 1;
		}
	}

	environmentDestroyInvalidateTokens(environment);
	delete tokens;
	return 0;
}
