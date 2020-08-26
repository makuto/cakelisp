#include "ParserGenerator.hpp"

#include "Utilities.hpp"

enum ParserGeneratorStateType
{
	ParserGeneratorStateType_None,
	ParserGeneratorStateType_FunctionInvocation
};

struct ParserGeneratorState
{
	ParserGeneratorStateType type;
	int startDepth;
	const Token* startTrigger;
	const Token* endTrigger;
};

int parserGenerateCode(const std::vector<Token>& tokens,
                       std::vector<GenerateOperation>& operationsOut)
{
	std::vector<ParserGeneratorState> stack;
	ParserGeneratorState defaultState;

	const Token* previousToken = nullptr;
	int depth = 0;
	for (const Token& token : tokens)
	{
		ParserGeneratorState* currentState = &defaultState;
		if (!stack.empty())
			currentState = &stack.back();

		printIndentToDepth(stack.size());

		if (currentState->type == ParserGeneratorStateType_FunctionInvocation)
		{
			if (previousToken && previousToken->type == TokenType_OpenParen)
			{
				// Increase depth
			}
			else if (token.type == TokenType_Symbol || token.type == TokenType_String)
			{
				printf("\t%s,\n", token.contents.c_str());
			}
			else if (token.type == TokenType_CloseParen && depth == currentState->startDepth)
			{
				// Finished with the operation
				printf(");\n");
				currentState->endTrigger = &token;
				currentState = &defaultState;
				// TODO convert to operation?
				stack.pop_back();
			}
		}

		if (previousToken && previousToken->type == TokenType_OpenParen)
		{
			if (token.type == TokenType_Symbol)
			{
				printf("Invoke function %s\n", token.contents.c_str());

				// operationsOut.push_back({"printf("});
				ParserGeneratorState newState = {ParserGeneratorStateType_FunctionInvocation, depth,
				                                 &token, nullptr};
				stack.push_back(newState);
			}
		}

		if (token.type == TokenType_OpenParen)
			depth++;
		else if (token.type == TokenType_CloseParen)
			depth--;

		previousToken = &token;
	}

	return 0;
}
