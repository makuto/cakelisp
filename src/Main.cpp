#include <stdio.h>
#include <vector>

#include "Tokenizer.hpp"

int main(int argc, char* argv[])
{
	if (argc != 2)
	{
		printf("Need to provide a file to parse\n");
		return 1;
	}

	FILE* file = nullptr;
	const char* filename = argv[1];
	file = fopen(filename, "r");
	if (!file)
	{
		printf("Error: Could not open %s\n", filename);
		return 1;
	}

	bool verbose = false;

	char lineBuffer[2048] = {0};
	int lineNumber = 1;
	std::vector<Token> tokens;
	while (fgets(lineBuffer, sizeof(lineBuffer), file))
	{
		if (verbose)
			printf("%s", lineBuffer);

		const char* error = tokenizeLine(lineBuffer, lineNumber, tokens);
		if (error != nullptr)
		{
			printf("%s:%d: %s\n", filename, lineNumber, error);
			return 1;
		}

		lineNumber++;
	}

	printf("\nResult:\n");

	int nestingDepth = 0;
	for (Token& token : tokens)
	{
		for (int i = 0; i < nestingDepth; ++i)
			printf("\t");

		printf("%s", tokenTypeToString(token.type));

		bool printRanges = true;
		if (printRanges)
		{
			printf("\t\tline %d, from line character %d to %d", token.lineNumber, token.columnStart,
			       token.columnEnd);
		}

		if (token.type == TokenType_OpenParen)
			++nestingDepth;
		else if (token.type == TokenType_CloseParen)
		{
			--nestingDepth;
			if (nestingDepth < 0)
			{
				printf("\nError: Mismatched parenthesis. Too many closing parentheses\n");
				return 1;
			}
		}

		if (!token.contents.empty())
		{
			printf("\n");
			for (int i = 0; i < nestingDepth; ++i)
				printf("\t");
			printf("\t%s\n", token.contents.c_str());
		}
		else
			printf("\n");
	}

	if (nestingDepth != 0)
	{
		printf("Error: Mismatched parenthesis. Missing closing parentheses\n");
		return 1;
	}

	fclose(file);

	return 0;
}
