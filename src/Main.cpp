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

	char lineBuffer[2048] = {0};
	int lineNumber = 1;
	std::vector<Token> tokens;
	while (fgets(lineBuffer, sizeof(lineBuffer), file))
	{
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

	for (Token& token : tokens)
	{
		printf("%s", tokenTypeToString(token.type));
		if (token.type == TokenType_Symbol && token.contents)
			printf("%s\n", token.contents);
		else
			printf("\n");
	}

	fclose(file);

	return 0;
}
