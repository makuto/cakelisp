#include "ParserGenerator.hpp"

#include "Converters.hpp"
#include "Utilities.hpp"

// TODO: Replace with fast hash table
#include <unordered_map>

// TODO: safe version of strcat
#include <stdio.h>
#include <string.h>

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

//
// Environment
//

typedef void (*GeneratorFunc)(const std::vector<Token>& tokens, int startTokenIndex,
                              GeneratorOutput& output);

// TODO: Replace with fast hash table
static std::unordered_map<std::string, GeneratorFunc> environmentGenerators;
typedef std::unordered_map<std::string, GeneratorFunc>::iterator GeneratorIterator;

void CImportGenerator(const std::vector<Token>& tokens, int startTokenIndex,
                      GeneratorOutput& output);
void DefunGenerator(const std::vector<Token>& tokens, int startTokenIndex, GeneratorOutput& output);

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

//
// Token parsing helpers
//

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

bool GeneratorExpectType(const char* generatorName, const Token& token, TokenType expectedType)
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
bool GeneratorExpectInInvocation(const char* message, const std::vector<Token>& tokens,
                                 int indexToCheck, int endInvocationIndex)
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
int getExpectedArgument(const char* message, const std::vector<Token>& tokens, int startTokenIndex,
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

	ErrorAtTokenf(tokens[endTokenIndex], "missing arguments: %s", message);
	return -1;
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

//
// Generators
//

void CImportGenerator(const std::vector<Token>& tokens, int startTokenIndex,
                      GeneratorOutput& output)
{
	int endTokenIndex = FindCloseParenTokenIndex(tokens, startTokenIndex);
	// Generators receive the entire invocation. We'll ignore it in this case
	StripInvocation(startTokenIndex, endTokenIndex);
	if (!GeneratorExpectInInvocation("expected path(s) to include", tokens, startTokenIndex,
	                                 endTokenIndex + 1))
		return;

	enum CImportState
	{
		WithDefinitions,
		WithDeclarations
	};
	CImportState state = WithDefinitions;

	for (int i = startTokenIndex; i <= endTokenIndex; ++i)
	{
		const Token& currentToken = tokens[i];

		if (currentToken.type == TokenType_Symbol && isSpecialSymbol(currentToken))
		{
			if (currentToken.contents.compare("&with-defs") == 0)
				state = WithDefinitions;
			else if (currentToken.contents.compare("&with-decls") == 0)
				state = WithDeclarations;
			else
			{
				ErrorAtToken(currentToken, "Unrecognized sentinel symbol");
				return;
			}

			continue;
		}
		else if (!GeneratorExpectType("c-import", currentToken, TokenType_String) ||
		         currentToken.contents.empty())
			continue;

		char includeBuffer[128] = {0};
		// #include <stdio.h> is passed in as "<stdio.h>", so we need a special case (no quotes)
		if (currentToken.contents[0] == '<')
		{
			PrintfBuffer(includeBuffer, "#include %s", currentToken.contents.c_str());
		}
		else
		{
			PrintfBuffer(includeBuffer, "#include \"%s\"", currentToken.contents.c_str());
		}

		if (state == WithDefinitions)
			output.source.push_back({std::string(includeBuffer), StringOutMod_NewlineAfter,
			                         &currentToken, &currentToken});
		else if (state == WithDeclarations)
			output.header.push_back({std::string(includeBuffer), StringOutMod_NewlineAfter,
			                         &currentToken, &currentToken});

		output.imports.push_back({currentToken.contents, ImportType_C, &currentToken});
	}
}

// Returns whether parsing was successful
bool tokenizedCTypeToString_Recursive(char* buffer, int bufferLength, char* afterNameBuffer,
                                      int afterNameBufferLength, const std::vector<Token>& tokens,
                                      int startTokenIndex, NameStyleMode typeNameMode,
                                      bool allowArray)
{
	// A type name
	if (tokens[startTokenIndex].type == TokenType_Symbol)
	{
		if (isSpecialSymbol(tokens[startTokenIndex]))
		{
			ErrorAtToken(tokens[startTokenIndex],
			             "types must not be : keywords or & sentinels. A generator may be "
			             "misinterpreting the special symbol, or you have made a mistake");
			return false;
		}

		// Convert lisp style type names to C style. This would normally be done by Convert*
		// StringOutputModifierFlags, but because we are returning a string directly, we'll do it
		// explicitly
		// TODO: Make C type output also use string modifiers?
		char convertedName[MAX_NAME_LENGTH] = {0};
		lispNameStyleToCNameStyle(typeNameMode, tokens[startTokenIndex].contents.c_str(),
		                          convertedName, sizeof(convertedName));

		// TODO: Check for length
		strcat(buffer, convertedName);
		return true;
	}
	else
	{
		// Some examples:
		// (const int)
		// (* (const char))
		// (& (const (<> std::vector Token)))
		// ([] (const char))
		// ([] ([] 10 float)) ;; 2D Array with one specified dimension

		const Token& typeInvocation = tokens[startTokenIndex + 1];
		if (!GeneratorExpectType("C/C++ type parser generator", typeInvocation, TokenType_Symbol))
			return false;

		int endTokenIndex = FindCloseParenTokenIndex(tokens, startTokenIndex);

		if (typeInvocation.contents.compare("const") == 0)
		{
			// Prepend const-ness
			strcat(buffer, "const ");
			int typeIndex = getExpectedArgument("const requires type", tokens, startTokenIndex, 1,
			                                    endTokenIndex);
			if (typeIndex == -1)
				return false;

			return tokenizedCTypeToString_Recursive(buffer, bufferLength, afterNameBuffer,
			                                        afterNameBufferLength, tokens, typeIndex,
			                                        typeNameMode, allowArray);
		}
		else if (typeInvocation.contents.compare("*") == 0 ||
		         typeInvocation.contents.compare("&") == 0)
		{
			// Append pointer/reference
			int typeIndex =
			    getExpectedArgument("expected type", tokens, startTokenIndex, 1, endTokenIndex);
			if (typeIndex == -1)
				return false;

			if (!tokenizedCTypeToString_Recursive(buffer, bufferLength, afterNameBuffer,
			                                      afterNameBufferLength, tokens, typeIndex,
			                                      typeNameMode, allowArray))
				return false;

			strcat(buffer, typeInvocation.contents.c_str());
		}
		else if (typeInvocation.contents.compare("<>") == 0)
		{
			int typeIndex = getExpectedArgument("expected template name", tokens, startTokenIndex,
			                                    1, endTokenIndex);
			if (typeIndex == -1)
				return false;

			if (!tokenizedCTypeToString_Recursive(buffer, bufferLength, afterNameBuffer,
			                                      afterNameBufferLength, tokens, typeIndex,
			                                      typeNameMode, allowArray))
				return false;

			strcat(buffer, "<");
			for (int startTemplateParameter = typeIndex + 1; startTemplateParameter < endTokenIndex;
			     ++startTemplateParameter)
			{
				// Override allowArray for subsequent parsing, because otherwise, the array args
				// will be appended to the wrong buffer, and you cannot declare arrays in template
				// parameters anyways (as far as I can tell)
				if (!tokenizedCTypeToString_Recursive(buffer, bufferLength, afterNameBuffer,
				                                      afterNameBufferLength, tokens,
				                                      startTemplateParameter, typeNameMode, /*allowArray=*/false))
					return false;

				if (!isLastArgument(tokens, startTemplateParameter, endTokenIndex))
					strcat(buffer, ", ");

				// Skip over tokens of the type we just parsed (the for loop increment will move us
				// off the end paren)
				if (tokens[startTemplateParameter].type == TokenType_OpenParen)
					startTemplateParameter =
					    FindCloseParenTokenIndex(tokens, startTemplateParameter);
			}
			strcat(buffer, ">");
		}
		else if (typeInvocation.contents.compare("[]") == 0)
		{
			if (!allowArray)
			{
				ErrorAtToken(
				    tokens[startTokenIndex],
				    "cannot declare array in this context. You may need to use a pointer instead");
				return false;
			}

			int firstArgIndex = getExpectedArgument("expected type or array size", tokens,
			                                        startTokenIndex, 1, endTokenIndex);
			if (firstArgIndex == -1)
				return false;

			// Arrays must append their brackets after the name (must be in separate buffer)
			bool arraySizeIsFirstArgument = tokens[firstArgIndex].type == TokenType_Symbol &&
			                                std::isdigit(tokens[firstArgIndex].contents[0]);
			int typeIndex = firstArgIndex;
			if (arraySizeIsFirstArgument)
			{
				typeIndex = getExpectedArgument("expected array type", tokens, startTokenIndex, 2,
				                                endTokenIndex);
				if (typeIndex == -1)
					return false;

				// Array size specified as first argument
				strcat(afterNameBuffer, "[");
				strcat(afterNameBuffer, tokens[firstArgIndex].contents.c_str());
				strcat(afterNameBuffer, "]");
			}
			else
				strcat(afterNameBuffer, "[]");

			// Type parsing happens after the [] have already been appended because the array's type
			// may include another array dimension, which must be specified after the current array
			return tokenizedCTypeToString_Recursive(buffer, bufferLength, afterNameBuffer,
			                                        afterNameBufferLength, tokens, typeIndex,
			                                        typeNameMode, /*allowArray=*/true);
		}
		else
		{
			ErrorAtToken(typeInvocation, "unknown C/C++ type specifier");
			return false;
		}
		return true;
	}
}

void DefunGenerator(const std::vector<Token>& tokens, int startTokenIndex, GeneratorOutput& output)
{
	// TODO: Get this passed in from the top!
	NameStyleMode typeNameMode = NameStyleMode_PascalCaseIfPlural;
	int endDefunTokenIndex = FindCloseParenTokenIndex(tokens, startTokenIndex);
	int endTokenIndex = endDefunTokenIndex;
	int startNameTokenIndex = startTokenIndex;
	StripInvocation(startNameTokenIndex, endTokenIndex);

	int nameIndex = startNameTokenIndex;
	const Token& nameToken = tokens[nameIndex];
	if (!GeneratorExpectType("defun", nameToken, TokenType_Symbol))
		return;

	int argsIndex = nameIndex + 1;
	if (!GeneratorExpectInInvocation("defun expected name", tokens, argsIndex, endDefunTokenIndex))
		return;
	const Token& argsStart = tokens[argsIndex];
	if (!GeneratorExpectType("defun", argsStart, TokenType_OpenParen))
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
		int nameIndex;
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
			if (!GeneratorExpectType("defun", currentToken, TokenType_Symbol))
				return;
			if (isSpecialSymbol(currentToken))
			{
				ErrorAtTokenf(currentToken,
				              "defun expected argument name, but got symbol or marker %s",
				              currentToken.contents.c_str());
				return;
			}
			else
			{
				currentArgument.nameIndex = i;
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
				if (!GeneratorExpectInInvocation("&return expected type", tokens, i + 1,
				                                 endArgsIndex))
					return;
				// Wait until next token to get type
				continue;
			}

			if (currentToken.type != TokenType_OpenParen && currentToken.type != TokenType_Symbol)
			{
				ErrorAtTokenf(currentToken, "defun expected argument type, got %s",
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
			if (!GeneratorExpectInInvocation("expected argument name", tokens, i + 1, endArgsIndex))
				return;
		}
	}

	char returnTypeBuffer[128] = {0};
	char returnAfterNameBuffer[16] = {0};
	bool returnTypeValid = false;
	// Unspecified means void, unlike lisp, which deduces return
	if (returnTypeStart == -1)
	{
		PrintfBuffer(returnTypeBuffer, "%s", "void");
		returnTypeValid = true;
	}
	else
	{
		const Token& returnTypeToken = tokens[returnTypeStart];

		// Check whether any arguments followed return type, because they will be ignored
		{
			int returnTypeEndIndex = returnTypeStart;
			if (returnTypeToken.type == TokenType_OpenParen)
				returnTypeEndIndex = FindCloseParenTokenIndex(tokens, returnTypeStart);

			if (returnTypeEndIndex + 1 < endArgsIndex)
			{
				const Token& extraneousToken = tokens[returnTypeEndIndex + 1];
				ErrorAtToken(extraneousToken, "Arguments after &return type are ignored");
				return;
			}
		}

		returnTypeValid = tokenizedCTypeToString_Recursive(
		    returnTypeBuffer, sizeof(returnTypeBuffer), returnAfterNameBuffer,
		    sizeof(returnAfterNameBuffer), tokens, returnTypeStart, typeNameMode,
		    /*allowArray=*/false);
	}

	if (!returnTypeValid)
		return;

	if (returnTypeStart == -1)
	{
		// The type was implicit; blame the "defun"
		output.source.push_back({returnTypeBuffer, StringOutMod_SpaceAfter,
		                         &tokens[startTokenIndex], &tokens[startTokenIndex]});
		output.header.push_back({returnTypeBuffer, StringOutMod_SpaceAfter,
		                         &tokens[startTokenIndex], &tokens[startTokenIndex]});
	}
	else
	{
		const Token& returnTypeToken = tokens[returnTypeStart];
		const Token* returnTypeEndToken = &returnTypeToken;
		if (returnTypeToken.type != TokenType_Symbol)
			returnTypeEndToken = &tokens[FindCloseParenTokenIndex(tokens, startTokenIndex)];

		output.source.push_back(
		    {returnTypeBuffer, StringOutMod_SpaceAfter, &returnTypeToken, returnTypeEndToken});
		output.header.push_back(
		    {returnTypeBuffer, StringOutMod_SpaceAfter, &returnTypeToken, returnTypeEndToken});
	}

	output.source.push_back(
	    {nameToken.contents, StringOutMod_ConvertFunctionName, &nameToken, &nameToken});
	output.header.push_back(
	    {nameToken.contents, StringOutMod_ConvertFunctionName, &nameToken, &nameToken});

	output.source.push_back({"(", StringOutMod_None, &argsStart, &argsStart});
	output.header.push_back({"(", StringOutMod_None, &argsStart, &argsStart});

	// Output arguments
	int numFunctionArguments = arguments.size();
	for (int i = 0; i < numFunctionArguments; ++i)
	{
		const DefunArgument& arg = arguments[i];
		char typeBuffer[128] = {0};
		char afterNameBuffer[16] = {0};
		bool typeValid = tokenizedCTypeToString_Recursive(
		    typeBuffer, sizeof(typeBuffer), afterNameBuffer, sizeof(afterNameBuffer), tokens,
		    arg.startTypeIndex, typeNameMode, /*allowArray=*/true);
		if (!typeValid)
			return;

		// Type
		{
			output.source.push_back({typeBuffer, StringOutMod_SpaceAfter,
			                         &tokens[arg.startTypeIndex], &tokens[arg.nameIndex - 1]});
			output.header.push_back({typeBuffer, StringOutMod_SpaceAfter,
			                         &tokens[arg.startTypeIndex], &tokens[arg.nameIndex - 1]});
		}
		// Name
		output.source.push_back({tokens[arg.nameIndex].contents, StringOutMod_ConvertArgumentName,
		                         &tokens[arg.nameIndex], &tokens[arg.nameIndex]});
		output.header.push_back({tokens[arg.nameIndex].contents, StringOutMod_ConvertArgumentName,
		                         &tokens[arg.nameIndex], &tokens[arg.nameIndex]});
		// Array
		output.source.push_back({afterNameBuffer, StringOutMod_None, &tokens[arg.startTypeIndex],
		                         &tokens[arg.nameIndex - 1]});
		output.header.push_back({afterNameBuffer, StringOutMod_None, &tokens[arg.startTypeIndex],
		                         &tokens[arg.nameIndex - 1]});

		if (i + 1 < numFunctionArguments)
		{
			// It's a little weird to have to pick who is responsible for the comma. In this case
			// both the name and the type of the next arg are responsible, because there wouldn't be
			// a comma if there wasn't a next arg
			DefunArgument& nextArg = arguments[i + 1];
			output.source.push_back({",", StringOutMod_SpaceAfter, &tokens[arg.nameIndex],
			                         &tokens[nextArg.startTypeIndex]});
			output.header.push_back({",", StringOutMod_SpaceAfter, &tokens[arg.nameIndex],
			                         &tokens[nextArg.startTypeIndex]});
		}
	}

	output.source.push_back(
	    {")", StringOutMod_NewlineAfter, &tokens[endArgsIndex], &tokens[endArgsIndex]});
	// Forward declarations end with ;
	output.header.push_back(
	    {");", StringOutMod_NewlineAfter, &tokens[endArgsIndex], &tokens[endArgsIndex]});

	int startBodyIndex = endArgsIndex;
	output.source.push_back(
	    {"{", StringOutMod_NewlineAfter, &tokens[startBodyIndex], &tokens[startBodyIndex]});

	// TODO: Body

	output.source.push_back(
	    {"}", StringOutMod_NewlineAfter, &tokens[endTokenIndex], &tokens[endTokenIndex]});

	output.functions.push_back(
	    {nameToken.contents, &tokens[startTokenIndex], &tokens[endTokenIndex]});
}

int parserGenerateCode(const std::vector<Token>& tokens, GeneratorOutput& output)
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
				// printIndentToDepth(stack.size());
				// printf("\t%s,\n", token.contents.c_str());
			}
			else if (token.type == TokenType_CloseParen && depth == currentState->startDepth)
			{
				// Finished with the operation
				// printIndentToDepth(stack.size());
				// printf(");\n");
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
						ErrorAtTokenf(token,
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
						invokedGenerator(tokens, currentTokenIndex - 1, output);
					}
				}
				else
				{
					// printIndentToDepth(stack.size());
					// printf("Invoke function %s\n", token.contents.c_str());
					// output.source.push_back({"printf("});
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

const char* importTypeToString(ImportType type)
{
	switch (type)
	{
		case ImportType_None:
			return "None";
		case ImportType_C:
			return "C";
		case ImportType_Cakelisp:
			return "Cakelisp";
		default:
			return "Unknown";
	}
}

// TODO This should be automatically generated
NameStyleMode getNameStyleModeForFlags(NameStyleSettings& settings,
                                       StringOutputModifierFlags modifierFlags)
{
	NameStyleMode mode = NameStyleMode_None;
	if (modifierFlags & StringOutMod_ConvertTypeName)
	{
		if (mode)
			printf("\nWarning: Name was given conflicting convert flags: %d\n", (int)modifierFlags);
		mode = settings.typeNameMode;
	}
	if (modifierFlags & StringOutMod_ConvertFunctionName)
	{
		if (mode)
			printf("\nWarning: Name was given conflicting convert flags: %d\n", (int)modifierFlags);
		mode = settings.functionNameMode;
	}
	if (modifierFlags & StringOutMod_ConvertArgumentName)
	{
		if (mode)
			printf("\nWarning: Name was given conflicting convert flags: %d\n", (int)modifierFlags);
		mode = settings.argumentNameMode;
	}
	if (modifierFlags & StringOutMod_ConvertVariableName)
	{
		if (mode)
			printf("\nWarning: Name was given conflicting convert flags: %d\n", (int)modifierFlags);
		mode = settings.variableNameMode;
	}
	if (modifierFlags & StringOutMod_ConvertGlobalVariableName)
	{
		if (mode)
			printf("\nWarning: Name was given conflicting convert flags: %d\n", (int)modifierFlags);
		mode = settings.globalVariableNameMode;
	}

	return mode;
}

void debugPrintStringOutput(NameStyleSettings& settings, const StringOutput& outputOperation)
{
	NameStyleMode mode =
	    getNameStyleModeForFlags(settings, outputOperation.modifiers);
	if (mode)
	{
		char convertedName[128] = {0};
		lispNameStyleToCNameStyle(mode, outputOperation.output.c_str(), convertedName,
		                          sizeof(convertedName));
		printf("%s", convertedName);
	}
	else
		printf("%s", outputOperation.output.c_str());

	if (outputOperation.modifiers & StringOutMod_SpaceAfter)
		printf(" ");
	if (outputOperation.modifiers & StringOutMod_NewlineAfter)
		printf("\n");
}
