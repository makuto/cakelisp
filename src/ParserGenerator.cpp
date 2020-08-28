#include "ParserGenerator.hpp"

#include "Utilities.hpp"

// TODO: Replace with fast hash table
#include <unordered_map>

enum ParserGeneratorStateType
{
	ParserGeneratorStateType_None,
	ParserGeneratorStateType_FunctionInvocation,
	ParserGeneratorStateType_Error
};

struct ParserGeneratorState
{
	ParserGeneratorStateType type;
	int startDepth;
	const Token* startTrigger;
	const Token* endTrigger;
};

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

typedef void (*GeneratorFunc)(const char* filename, const std::vector<Token>& tokens,
                              int startTokenIndex, std::vector<GenerateOperation>& operationsOut);

// Note that the tokenizer should've already confirmed our parenthesis match, so we won't do
// validation here
int FindCloseParenTokenIndex(const std::vector<Token>& tokens, int startTokenIndex)
{
	if (tokens[startTokenIndex].type != TokenType_OpenParen)
		printf(
		    "Warning: FindCloseParenTokenIndex() expects to start on the opening parenthesis\n");

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

bool GeneratorExpectType(const char* filename, const char* generatorName, const Token& token,
                         TokenType expectedType)
{
	if (token.type != expectedType)
	{
		ErrorAtTokenf(filename, token, "%s expected %s, but got %s", generatorName,
		              tokenTypeToString(expectedType), tokenTypeToString(token.type));
		return false;
	}
	return true;
}

// Errors and returns false if out of invocation (or at closing paren)
bool GeneratorExpectInInvocation(const char* filename, const char* message,
                                 const std::vector<Token>& tokens, int indexToCheck,
                                 int endInvocationIndex)
{
	if (indexToCheck >= endInvocationIndex)
	{
		const Token& endToken = tokens[endInvocationIndex];
		ErrorAtToken(filename, endToken, message);
		return false;
	}
	return true;
}

void CImportGenerator(const char* filename, const std::vector<Token>& tokens, int startTokenIndex,
                      std::vector<GenerateOperation>& operationsOut)
{
	int endTokenIndex = FindCloseParenTokenIndex(tokens, startTokenIndex);
	// Generators receive the entire invocation. We'll ignore it in this case
	StripInvocation(startTokenIndex, endTokenIndex);
	if (!GeneratorExpectInInvocation(filename, "expected path to include", tokens, startTokenIndex,
	                                 endTokenIndex + 1))
		return;

	for (int i = startTokenIndex; i <= endTokenIndex; ++i)
	{
		const Token& currentToken = tokens[i];
		if (!GeneratorExpectType(filename, "c-import", currentToken, TokenType_String) ||
		    currentToken.contents.empty())
			continue;

		char includeBuffer[128] = {0};
		// #include <stdio.h> is passed in as "<stdio.h>", so we need a special case (no quotes)
		if (currentToken.contents[0] == '<')
		{
			PrintfBuffer(includeBuffer, "#include %s\n", currentToken.contents.c_str());
		}
		else
		{
			PrintfBuffer(includeBuffer, "#include \"%s\"\n", currentToken.contents.c_str());
		}
		operationsOut.push_back({std::string(includeBuffer), &currentToken, &currentToken});
	}
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

// Returns whether parsing was successful
bool tokenizedCTypeToString(const char* filename, char* buffer, int bufferLength,
                            const std::vector<Token>& tokens, int startTokenIndex)
{
	if (tokens[startTokenIndex].type == TokenType_Symbol &&
	    !isSpecialSymbol(tokens[startTokenIndex]))
	{
		SafeSnprinf(buffer, bufferLength, "%s", tokens[startTokenIndex].contents.c_str());
		return true;
	}
	else
	{
		ErrorAtToken(filename, tokens[startTokenIndex], "Type could not be parsed");
		// TODO Parse type
		return false;
	}
}

void DefunGenerator(const char* filename, const std::vector<Token>& tokens, int startTokenIndex,
                    std::vector<GenerateOperation>& operationsOut)
{
	int endDefunTokenIndex = FindCloseParenTokenIndex(tokens, startTokenIndex);
	int endTokenIndex = endDefunTokenIndex;
	int startNameTokenIndex = startTokenIndex;
	StripInvocation(startNameTokenIndex, endTokenIndex);

	int nameIndex = startNameTokenIndex;
	const Token& nameToken = tokens[nameIndex];
	if (!GeneratorExpectType(filename, "defun", nameToken, TokenType_Symbol))
		return;

	int argsIndex = nameIndex + 1;
	if (!GeneratorExpectInInvocation(filename, "defun expected name", tokens, argsIndex, endDefunTokenIndex))
		return;
	const Token& argsStart = tokens[argsIndex];
	if (!GeneratorExpectType(filename, "defun", argsStart, TokenType_OpenParen))
		return;

	enum DefunState
	{
		Type,
		Name,
		ReturnType
	};

	DefunState state = Type;
	int returnTypeStart = -1;

	struct DefunArgument
	{
		int startTypeIndex;
		const Token* name;
	};
	std::vector<DefunArgument> arguments;
	DefunArgument currentArgument = {};

	int endArgsIndex = FindCloseParenTokenIndex(tokens, argsIndex);
	for (int i = argsIndex + 1; i < endArgsIndex; ++i)
	{
		const Token& currentToken = tokens[i];

		if (state == ReturnType)
		{
			returnTypeStart = i;
			break;
		}
		else if (state == Name)
		{
			if (!GeneratorExpectType(filename, "defun", currentToken, TokenType_Symbol))
				return;
			if (isSpecialSymbol(currentToken))
			{
				ErrorAtTokenf(filename, currentToken,
				              "defun expected argument name, but got symbol or marker %s",
				              currentToken.contents.c_str());
				return;
			}
			else
			{
				currentArgument.name = &currentToken;
				// Finished with an argument
				arguments.push_back(currentArgument);
				currentArgument = {};

				state = Type;
			}
		}
		else if (state == Type)
		{
			if (currentToken.type == TokenType_Symbol &&
			    currentToken.contents.compare("&return") == 0)
			{
				state = ReturnType;
				if (!GeneratorExpectInInvocation(filename, "&return expected type", tokens, i + 1, endArgsIndex))
					return;
				// Wait until next token to get type
				continue;
			}

			if (currentToken.type != TokenType_OpenParen && currentToken.type != TokenType_Symbol)
			{
				ErrorAtTokenf(filename, currentToken, "defun expected argument type, got %s",
				              tokenTypeToString(currentToken.type));
				return;
			}

			currentArgument.startTypeIndex = i;
			state = Name;
			// Skip past type declaration; it will be handled later
			if (currentToken.type == TokenType_OpenParen)
			{
				i = FindCloseParenTokenIndex(tokens, i);
			}

			// We've now introduced an expectation that a name will follow
			if (!GeneratorExpectInInvocation(filename, "expected argument name", tokens, i + 1, endArgsIndex))
				return;
		}
	}

	for (DefunArgument arg : arguments)
	{
		char typeBuffer[128] = {0};
		bool typeValid = tokenizedCTypeToString(filename, typeBuffer, sizeof(typeBuffer), tokens,
		                                        arg.startTypeIndex);
		if (!typeValid)
			return;
	}

	char returnTypeBuffer[128] = {0};
	bool returnTypeValid = false;
	// Unspecified means void, unlike lisp, which deduces return
	if (returnTypeStart == -1)
	{
		PrintfBuffer(returnTypeBuffer, "%s", "void");
		returnTypeValid = true;
	}
	else
	{
		returnTypeValid = tokenizedCTypeToString(filename, returnTypeBuffer,
		                                         sizeof(returnTypeBuffer), tokens, returnTypeStart);
	}

	if (!returnTypeValid)
		return;

	if (returnTypeStart == -1)
	{
		// TODO: Come up with better thing for this? Operation padding options?
		char returnTypeBufferWithSpace[128] = {0};
		PrintfBuffer(returnTypeBufferWithSpace, "%s ", returnTypeBuffer);
		// The type was implicit; blame the "defun"
		operationsOut.push_back(
		    {returnTypeBufferWithSpace, &tokens[startTokenIndex], &tokens[startTokenIndex]});
	}
	else
	{
		const Token& returnTypeToken = tokens[returnTypeStart];
		const Token* returnTypeEndToken = &returnTypeToken;
		if (returnTypeToken.type != TokenType_Symbol)
			returnTypeEndToken = &tokens[FindCloseParenTokenIndex(tokens, startTokenIndex)];

		// TODO: Come up with better thing for this? Operation padding options?
		char returnTypeBufferWithSpace[128] = {0};
		PrintfBuffer(returnTypeBufferWithSpace, "%s ", returnTypeBuffer);
		operationsOut.push_back({returnTypeBufferWithSpace, &returnTypeToken, returnTypeEndToken});
	}

	operationsOut.push_back({nameToken.contents, &nameToken, &nameToken});

	operationsOut.push_back({"(", &argsStart, &argsStart});
	operationsOut.push_back({")", &tokens[endArgsIndex], &tokens[endArgsIndex]});
}

// TODO: Replace with fast hash table
static std::unordered_map<std::string, GeneratorFunc> environmentGenerators;
typedef std::unordered_map<std::string, GeneratorFunc>::iterator GeneratorIterator;

GeneratorFunc findGenerator(const char* functionName)
{
	// For testing only: Lazy-initialize the bootstrapping/fundamental generators
	if (environmentGenerators.empty())
	{
		environmentGenerators["c-import"] = CImportGenerator;
		environmentGenerators["defun"] = DefunGenerator;
	}

	GeneratorIterator findIt = environmentGenerators.find(std::string(functionName));
	if (findIt != environmentGenerators.end())
		return findIt->second;
	return nullptr;
}

int parserGenerateCode(const char* filename, const std::vector<Token>& tokens,
                       std::vector<GenerateOperation>& operationsOut)
{
	std::vector<ParserGeneratorState> stack;
	ParserGeneratorState defaultState;

	int numErrors = 0;

	const Token* previousToken = nullptr;
	int depth = 0;
	int currentTokenIndex = 0;
	for (const Token& token : tokens)
	{
		ParserGeneratorState* currentState = &defaultState;
		if (!stack.empty())
			currentState = &stack.back();

		if (currentState->type == ParserGeneratorStateType_Error)
		{
			if (token.type == TokenType_CloseParen && depth == currentState->startDepth)
			{
				// Finished with the operation
				printIndentToDepth(stack.size());
				printf("End error\n");
				currentState->endTrigger = &token;
				currentState = &defaultState;
				// TODO convert to operation?
				stack.pop_back();
			}
		}
		// TODO The whole function invocation block might just be "ignore everything", because
		// generators handle these
		else if (currentState->type == ParserGeneratorStateType_FunctionInvocation)
		{
			if (previousToken && previousToken->type == TokenType_OpenParen)
			{
				// Increase depth
			}
			else if (token.type == TokenType_Symbol || token.type == TokenType_String)
			{
				printIndentToDepth(stack.size());
				printf("\t%s,\n", token.contents.c_str());
			}
			else if (token.type == TokenType_CloseParen && depth == currentState->startDepth)
			{
				// Finished with the operation
				printIndentToDepth(stack.size());
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
				if (depth == 1)
				{
					GeneratorFunc invokedGenerator = findGenerator(token.contents.c_str());
					if (!invokedGenerator)
					{
						ErrorAtTokenf(filename, token,
						              "Unknown function %s. Only macros/generators may be invoked "
						              "at top level",
						              token.contents.c_str());
						++numErrors;

						ParserGeneratorState newState = {ParserGeneratorStateType_Error, depth,
						                                 &token, nullptr};
						stack.push_back(newState);
					}
					else
					{
						// Give the entire invocation (go back one to get open paren)
						invokedGenerator(filename, tokens, currentTokenIndex - 1, operationsOut);
					}
				}
				else
				{
					printIndentToDepth(stack.size());
					printf("Invoke function %s\n", token.contents.c_str());
					// operationsOut.push_back({"printf("});
					ParserGeneratorState newState = {ParserGeneratorStateType_FunctionInvocation,
					                                 depth, &token, nullptr};
					stack.push_back(newState);
				}
			}
		}

		if (token.type == TokenType_OpenParen)
			depth++;
		else if (token.type == TokenType_CloseParen)
			depth--;

		previousToken = &token;
		++currentTokenIndex;
	}

	return numErrors;
}
