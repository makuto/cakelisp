#include "GeneratorHelpers.hpp"

#include "Evaluator.hpp"
#include "Tokenizer.hpp"
#include "Utilities.hpp"

// Generators receive the entire invocation. This function makes it easy to strip it away. It is
// useful to get the whole invocation in case the same generator is used with multiple different
// invocation strings
void StripInvocation(int& startTokenIndex, int& endTokenIndex)
{
	// ignore the "(blah"
	startTokenIndex += 2;
	// Ignore the final closing paren
	endTokenIndex -= 1;
}

// Note that the tokenizer should've already confirmed our parenthesis match, so we won't do
// validation here
int FindCloseParenTokenIndex(const std::vector<Token>& tokens, int startTokenIndex)
{
	if (tokens[startTokenIndex].type != TokenType_OpenParen)
		printf("Warning: FindCloseParenTokenIndex() expects to start on the opening parenthesis\n");

	int depth = 0;
	int numTokens = tokens.size();
	for (int i = startTokenIndex; i < numTokens; ++i)
	{
		const Token* token = &tokens[i];
		if (token->type == TokenType_OpenParen)
			depth++;
		else if (token->type == TokenType_CloseParen)
			depth--;

		if (depth == 0)
			return i;
	}

	return tokens.size();
}

bool ExpectEvaluatorScope(const char* generatorName, const Token& token,
                          const EvaluatorContext& context, EvaluatorScope expectedScope)
{
	if (context.scope != expectedScope)
	{
		ErrorAtTokenf(token, "%s expected to be invoked in %s scope, but is in %s scope",
		              generatorName, evaluatorScopeToString(expectedScope),
		              evaluatorScopeToString(context.scope));
		return false;
	}
	return true;
}

bool IsForbiddenEvaluatorScope(const char* generatorName, const Token& token,
                               const EvaluatorContext& context, EvaluatorScope forbiddenScope)
{
	if (context.scope == forbiddenScope)
	{
		ErrorAtTokenf(token, "%s cannot be invoked in %s scope", generatorName,
		              evaluatorScopeToString(forbiddenScope));
		return true;
	}
	return false;
}

bool ExpectTokenType(const char* generatorName, const Token& token, TokenType expectedType)
{
	if (token.type != expectedType)
	{
		ErrorAtTokenf(token, "%s expected %s, but got %s", generatorName,
		              tokenTypeToString(expectedType), tokenTypeToString(token.type));
		return false;
	}
	return true;
}

// Errors and returns false if out of invocation (or at closing paren)
bool ExpectInInvocation(const char* message, const std::vector<Token>& tokens, int indexToCheck,
                        int endInvocationIndex)
{
	if (indexToCheck >= endInvocationIndex)
	{
		const Token& endToken = tokens[endInvocationIndex];
		ErrorAtToken(endToken, message);
		return false;
	}
	return true;
}

// TODO: Come up with better name
bool isSpecialSymbol(const Token& token)
{
	if (token.type == TokenType_Symbol)
	{
		// The size check allows functions to be declared named ':' or '&', but not ':bad' or '&bad'
		return token.contents.size() > 1 && (token.contents[0] == ':' || token.contents[0] == '&');
	}
	else
	{
		printf("Warning: isSpecialSymbol() expects only Symbol types\n");
		return true;
	}
}

// This function would be simpler and faster if there was an actual syntax tree, because we wouldn't
// be repeatedly traversing all the arguments
int getArgument(const std::vector<Token>& tokens, int startTokenIndex,
                        int desiredArgumentIndex, int endTokenIndex)
{
	int currentArgumentIndex = 0;
	for (int i = startTokenIndex + 1; i < endTokenIndex; ++i)
	{
		if (currentArgumentIndex == desiredArgumentIndex)
			return i;

		const Token& token = tokens[i];
		if (token.type == TokenType_OpenParen)
		{
			// Skip any nesting
			i = FindCloseParenTokenIndex(tokens, i);
		}

		++currentArgumentIndex;
	}

	return -1;
}

int getExpectedArgument(const char* message, const std::vector<Token>& tokens, int startTokenIndex,
                        int desiredArgumentIndex, int endTokenIndex)
{
	int argumentIndex = getArgument(tokens, startTokenIndex, desiredArgumentIndex, endTokenIndex);

	if (argumentIndex == -1)
		ErrorAtTokenf(tokens[endTokenIndex], "missing arguments: %s", message);

	return argumentIndex;
}

int getNumArguments(const std::vector<Token>& tokens, int startTokenIndex, int endTokenIndex)
{
	int currentArgumentIndex = 0;
	for (int i = startTokenIndex + 1; i < endTokenIndex; ++i)
	{
		const Token& token = tokens[i];
		if (token.type == TokenType_OpenParen)
		{
			// Skip any nesting
			i = FindCloseParenTokenIndex(tokens, i);
		}

		++currentArgumentIndex;
	}
	return currentArgumentIndex;
}

bool ExpectNumArguments(const std::vector<Token>& tokens, int startTokenIndex, int endTokenIndex,
                        int numExpectedArguments)
{
	int numArguments = getNumArguments(tokens, startTokenIndex, endTokenIndex);
	if (numArguments != numExpectedArguments)
	{
		ErrorAtTokenf(tokens[startTokenIndex],
		              "expected %d arguments, got %d (counts include invocation as first argument)",
		              numExpectedArguments, numArguments);
		return false;
	}
	return true;
}

bool isLastArgument(const std::vector<Token>& tokens, int startTokenIndex, int endTokenIndex)
{
	if (tokens[startTokenIndex].type == TokenType_OpenParen &&
	    FindCloseParenTokenIndex(tokens, startTokenIndex) + 1 < endTokenIndex)
		return false;
	else if (tokens[startTokenIndex].type == TokenType_Symbol &&
	         startTokenIndex + 1 < endTokenIndex)
		return false;
	return true;
}

int getNextArgument(const std::vector<Token>& tokens, int currentTokenIndex,
                     int endArrayTokenIndex)
{
	int nextArgStart = currentTokenIndex;
	if (tokens[currentTokenIndex].type == TokenType_OpenParen)
		nextArgStart = FindCloseParenTokenIndex(tokens, currentTokenIndex);

	++nextArgStart;
	return nextArgStart;
}

void MakeUniqueSymbolName(EvaluatorEnvironment& environment, const char* prefix,
                          Token* tokenToChange)
{
	char symbolNameBuffer[64] = {0};
	PrintfBuffer(symbolNameBuffer, "%s_%d", prefix, environment.nextFreeUniqueSymbolNum);

	tokenToChange->type = TokenType_Symbol;
	tokenToChange->contents = symbolNameBuffer;
	// TODO: If generated files are being checked in, it would be nice to have it be stable based on
	// file name or something
	environment.nextFreeUniqueSymbolNum++;
}

//
// Token list manipulation
//

void PushBackTokenExpression(std::vector<Token>& output, const Token* startToken)
{
	if (!startToken)
	{
		printf("error: PushBackTokenExpression() received null token\n");
		return;
	}

	if (startToken->type != TokenType_OpenParen)
	{
		output.push_back(*startToken);
	}
	else
	{
		int depth = 0;
		for (const Token* currentToken = startToken; depth >= 0; ++currentToken)
		{
			if (currentToken->type == TokenType_OpenParen)
				++depth;
			else if (currentToken->type == TokenType_CloseParen)
			{
				--depth;
				if (depth < 0)
					break;
			}

			output.push_back(*currentToken);
		}
	}
}

//
// Outputting
//

void addModifierToStringOutput(StringOutput& operation, StringOutputModifierFlags flag)
{
	operation.modifiers = (StringOutputModifierFlags)((int)operation.modifiers | (int)flag);
}

void addStringOutput(std::vector<StringOutput>& output, const std::string& symbol,
                     StringOutputModifierFlags modifiers, const Token* startToken)
{
	StringOutput newStringOutput = {};
	newStringOutput.modifiers = modifiers;
	newStringOutput.startToken = startToken;

	newStringOutput.output = symbol;

	output.push_back(std::move(newStringOutput));
}

void addLangTokenOutput(std::vector<StringOutput>& output, StringOutputModifierFlags modifiers,
                        const Token* startToken)
{
	StringOutput newStringOutput = {};
	newStringOutput.modifiers = modifiers;
	newStringOutput.startToken = startToken;

	output.push_back(std::move(newStringOutput));
}

void addSpliceOutput(std::vector<StringOutput>& output, GeneratorOutput* spliceOutput,
                     const Token* startToken)
{
	StringOutput newStringOutput = {};
	// No other modifiers are valid because splice is handled outside the normal writer
	newStringOutput.modifiers = StringOutMod_Splice;
	newStringOutput.startToken = startToken;

	newStringOutput.spliceOutput = spliceOutput;

	output.push_back(std::move(newStringOutput));
}
