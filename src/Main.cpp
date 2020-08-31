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

	printf("Tokenized %d lines\n", lineNumber - 1);

	if (!validateParentheses(tokens))
		return 1;

	bool printTokenizerOutput = false;
	if (printTokenizerOutput)
	{
		printf("\nResult:\n");

		// No need to validate, we already know it's safe
		int nestingDepth = 0;
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

	printf("\nParsing and code generation:\n");

	GeneratorOutput generatedOutput;
	if (parserGenerateCode(tokens, generatedOutput) != 0)
		return 1;
	else
	{
		NameStyleSettings nameSettings;

		printf("\nResult:\n");

		printGeneratorOutput(generatedOutput, nameSettings);
	}

	return 0;
}
