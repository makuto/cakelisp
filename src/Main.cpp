#include <stdio.h>
#include <vector>

#include "ParserGenerator.hpp"
#include "Tokenizer.hpp"
#include "Utilities.hpp"

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
	std::vector<Token> tokens;
	while (fgets(lineBuffer, sizeof(lineBuffer), file))
	{
		if (verbose)
			printf("%s", lineBuffer);

		const char* error = tokenizeLine(lineBuffer, filename, lineNumber, tokens);
		if (error != nullptr)
		{
			printf("%s:%d: error: %s\n", filename, lineNumber, error);
			return 1;
		}

		lineNumber++;
	}

	printf("\nResult:\n");

	int nestingDepth = 0;
	const Token* lastTopLevelOpenParen = nullptr;
	for (const Token& token : tokens)
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
			if (nestingDepth == 0)
				lastTopLevelOpenParen = &token;
			++nestingDepth;
		}
		else if (token.type == TokenType_CloseParen)
		{
			--nestingDepth;
			if (nestingDepth < 0)
			{
				ErrorAtToken(token,
				             "Mismatched parenthesis. Too many closing parentheses, or missing "
				             "opening parenthesies");
				return 1;
			}
		}

		if (!token.contents.empty())
		{
			printIndentToDepth(nestingDepth);
			printf("\t%s\n", token.contents.c_str());
		}
	}

	if (nestingDepth != 0)
	{
		ErrorAtToken(
		    *lastTopLevelOpenParen,
		    "Mismatched parenthesis. Missing closing parentheses, or too many opening parentheses");
		return 1;
	}

	fclose(file);

	printf("\nParsing and code generation:\n");

	GeneratorOutput generatedOutput;
	if (parserGenerateCode(tokens, generatedOutput) != 0)
		return 1;
	else
	{
		NameStyleSettings nameSettings;

		printf("\nResult:\n");

		printf("\tTo source file:\n");
		for (const StringOutput& operation : generatedOutput.source)
		{
			debugPrintStringOutput(nameSettings, operation);
		}

		printf("\n\tTo header file:\n");
		for (const StringOutput& operation : generatedOutput.header)
		{
			debugPrintStringOutput(nameSettings, operation);
		}

		printf("\n\tImports:\n");
		for (const ImportMetadata& import : generatedOutput.imports)
		{
			printf("%s\t(%s)\n", import.importName.c_str(), importTypeToString(import.type));
		}

		printf("\n\tFunctions:\n");
		for (const FunctionMetadata& function : generatedOutput.functions)
		{
			printf("%s\n", function.name.c_str());
		}
	}

	return 0;
}
