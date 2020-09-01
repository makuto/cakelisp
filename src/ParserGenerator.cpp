#include "ParserGenerator.hpp"

#include "Converters.hpp"
#include "Utilities.hpp"

// TODO: safe version of strcat
#include <stdio.h>
#include <string.h>

//
// Environment
//

bool CImportGenerator(EvaluatorEnvironment& environment, const EvaluatorContext& context,
                      const std::vector<Token>& tokens, int startTokenIndex,
                      GeneratorOutput& output);
bool DefunGenerator(EvaluatorEnvironment& environment, const EvaluatorContext& context,
                    const std::vector<Token>& tokens, int startTokenIndex, GeneratorOutput& output);

bool SquareMacro(EvaluatorEnvironment& environment, const EvaluatorContext& context,
                 const std::vector<Token>& tokens, int startTokenIndex, std::vector<Token>& output);

GeneratorFunc findGenerator(EvaluatorEnvironment& environment, const char* functionName)
{
	// For testing only: Lazy-initialize the bootstrapping/fundamental generators
	if (environment.generators.empty())
	{
		environment.generators["c-import"] = CImportGenerator;
		environment.generators["defun"] = DefunGenerator;
	}

	GeneratorIterator findIt = environment.generators.find(std::string(functionName));
	if (findIt != environment.generators.end())
		return findIt->second;
	return nullptr;
}

MacroFunc findMacro(EvaluatorEnvironment& environment, const char* functionName)
{
	// For testing only: Lazy-initialize the bootstrapping/fundamental generators
	if (environment.macros.empty())
	{
		// environment.macros["c-import"] = CImportGenerator;
		// For testing
		environment.macros["square"] = SquareMacro;
	}

	MacroIterator findIt = environment.macros.find(std::string(functionName));
	if (findIt != environment.macros.end())
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

const char* evaluatorScopeToString(EvaluatorScope expectedScope)
{
	switch (expectedScope)
	{
		case EvaluatorScope_Module:
			return "module";
		case EvaluatorScope_Body:
			return "body";
		case EvaluatorScope_ExpressionsOnly:
			return "expressions-only";
		default:
			return "unknown";
	}
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

void addModifierToStringOutput(StringOutput& operation, StringOutputModifierFlags flag)
{
	operation.modifiers = (StringOutputModifierFlags)((int)operation.modifiers | (int)flag);
}

//
// Generators
//

bool CImportGenerator(EvaluatorEnvironment& environment, const EvaluatorContext& context,
                      const std::vector<Token>& tokens, int startTokenIndex,
                      GeneratorOutput& output)
{
	if (!ExpectEvaluatorScope("c-import", tokens[startTokenIndex], context, EvaluatorScope_Module))
		return false;

	int endTokenIndex = FindCloseParenTokenIndex(tokens, startTokenIndex);
	// Generators receive the entire invocation. We'll ignore it in this case
	StripInvocation(startTokenIndex, endTokenIndex);
	if (!ExpectInInvocation("expected path(s) to include", tokens, startTokenIndex,
	                        endTokenIndex + 1))
		return false;

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
				return false;
			}

			continue;
		}
		else if (!ExpectTokenType("c-import", currentToken, TokenType_String) ||
		         currentToken.contents.empty())
			continue;

		// TODO: Convert to StringOutputs?
		char includeBuffer[MAX_PATH_LENGTH] = {0};
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

		output.imports.push_back({currentToken.contents, ImportLanguage_C, &currentToken});
	}

	return true;
}

// afterNameOutput must be a separate buffer because some C type specifiers (e.g. array []) need to
// come after the type. Returns whether parsing was successful
bool tokenizedCTypeToString_Recursive(const std::vector<Token>& tokens, int startTokenIndex,
                                      bool allowArray, std::vector<StringOutput>& typeOutput,
                                      std::vector<StringOutput>& afterNameOutput)
{
	if (&typeOutput == &afterNameOutput)
	{
		printf(
		    "Error: tokenizedCTypeToString_Recursive() requires a separate output buffer for "
		    "after-name types\n");
		return false;
	}

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

		typeOutput.push_back({tokens[startTokenIndex].contents.c_str(),
		                      StringOutMod_ConvertTypeName, &tokens[startTokenIndex],
		                      &tokens[startTokenIndex]});

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
		if (!ExpectTokenType("C/C++ type parser generator", typeInvocation, TokenType_Symbol))
			return false;

		int endTokenIndex = FindCloseParenTokenIndex(tokens, startTokenIndex);

		if (typeInvocation.contents.compare("const") == 0)
		{
			// Prepend const-ness
			typeOutput.push_back(
			    {"const", StringOutMod_SpaceAfter, &typeInvocation, &typeInvocation});

			int typeIndex = getExpectedArgument("const requires type", tokens, startTokenIndex, 1,
			                                    endTokenIndex);
			if (typeIndex == -1)
				return false;

			return tokenizedCTypeToString_Recursive(tokens, typeIndex, allowArray, typeOutput,
			                                        afterNameOutput);
		}
		else if (typeInvocation.contents.compare("*") == 0 ||
		         typeInvocation.contents.compare("&") == 0)
		{
			// Append pointer/reference
			int typeIndex =
			    getExpectedArgument("expected type", tokens, startTokenIndex, 1, endTokenIndex);
			if (typeIndex == -1)
				return false;

			if (!tokenizedCTypeToString_Recursive(tokens, typeIndex, allowArray, typeOutput,
			                                      afterNameOutput))
				return false;

			typeOutput.push_back({typeInvocation.contents.c_str(), StringOutMod_None,
			                      &typeInvocation, &typeInvocation});
		}
		else if (typeInvocation.contents.compare("<>") == 0)
		{
			int typeIndex = getExpectedArgument("expected template name", tokens, startTokenIndex,
			                                    1, endTokenIndex);
			if (typeIndex == -1)
				return false;

			if (!tokenizedCTypeToString_Recursive(tokens, typeIndex, allowArray, typeOutput,
			                                      afterNameOutput))
				return false;

			typeOutput.push_back({"<", StringOutMod_None, &typeInvocation, &typeInvocation});
			for (int startTemplateParameter = typeIndex + 1; startTemplateParameter < endTokenIndex;
			     ++startTemplateParameter)
			{
				// Override allowArray for subsequent parsing, because otherwise, the array args
				// will be appended to the wrong buffer, and you cannot declare arrays in template
				// parameters anyways (as far as I can tell)
				if (!tokenizedCTypeToString_Recursive(tokens, startTemplateParameter,
				                                      /*allowArray=*/false, typeOutput,
				                                      afterNameOutput))
					return false;

				if (!isLastArgument(tokens, startTemplateParameter, endTokenIndex))
					typeOutput.push_back({",", StringOutMod_SpaceAfter,
					                      &tokens[startTemplateParameter],
					                      &tokens[startTemplateParameter]});

				// Skip over tokens of the type we just parsed (the for loop increment will move us
				// off the end paren)
				if (tokens[startTemplateParameter].type == TokenType_OpenParen)
					startTemplateParameter =
					    FindCloseParenTokenIndex(tokens, startTemplateParameter);
			}
			typeOutput.push_back({">", StringOutMod_None, &typeInvocation, &typeInvocation});
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
				afterNameOutput.push_back(
				    {"[", StringOutMod_None, &typeInvocation, &typeInvocation});
				afterNameOutput.push_back({tokens[firstArgIndex].contents.c_str(),
				                           StringOutMod_None, &tokens[firstArgIndex],
				                           &tokens[firstArgIndex]});
				afterNameOutput.push_back(
				    {"]", StringOutMod_None, &typeInvocation, &typeInvocation});
			}
			else
				afterNameOutput.push_back(
				    {"[]", StringOutMod_None, &typeInvocation, &typeInvocation});

			// Type parsing happens after the [] have already been appended because the array's type
			// may include another array dimension, which must be specified after the current array
			return tokenizedCTypeToString_Recursive(tokens, typeIndex,
			                                        /*allowArray=*/true, typeOutput,
			                                        afterNameOutput);
		}
		else
		{
			ErrorAtToken(typeInvocation, "unknown C/C++ type specifier");
			return false;
		}
		return true;
	}
}

bool DefunGenerator(EvaluatorEnvironment& environment, const EvaluatorContext& context,
                    const std::vector<Token>& tokens, int startTokenIndex, GeneratorOutput& output)
{
	if (!ExpectEvaluatorScope("defun", tokens[startTokenIndex], context, EvaluatorScope_Module))
		return false;

	int endDefunTokenIndex = FindCloseParenTokenIndex(tokens, startTokenIndex);
	int endTokenIndex = endDefunTokenIndex;
	int startNameTokenIndex = startTokenIndex;
	StripInvocation(startNameTokenIndex, endTokenIndex);

	int nameIndex = startNameTokenIndex;
	const Token& nameToken = tokens[nameIndex];
	if (!ExpectTokenType("defun", nameToken, TokenType_Symbol))
		return false;

	int argsIndex = nameIndex + 1;
	if (!ExpectInInvocation("defun expected name", tokens, argsIndex, endDefunTokenIndex))
		return false;
	const Token& argsStart = tokens[argsIndex];
	if (!ExpectTokenType("defun", argsStart, TokenType_OpenParen))
		return false;

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
			if (!ExpectTokenType("defun", currentToken, TokenType_Symbol))
				return false;
			if (isSpecialSymbol(currentToken))
			{
				ErrorAtTokenf(currentToken,
				              "defun expected argument name, but got symbol or marker %s",
				              currentToken.contents.c_str());
				return false;
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
				if (!ExpectInInvocation("&return expected type", tokens, i + 1, endArgsIndex))
					return false;
				// Wait until next token to get type
				continue;
			}

			if (currentToken.type != TokenType_OpenParen && currentToken.type != TokenType_Symbol)
			{
				ErrorAtTokenf(currentToken, "defun expected argument type, got %s",
				              tokenTypeToString(currentToken.type));
				return false;
			}

			currentArgument.startTypeIndex = i;
			state = Name;
			// Skip past type declaration; it will be handled later
			if (currentToken.type == TokenType_OpenParen)
			{
				i = FindCloseParenTokenIndex(tokens, i);
			}

			// We've now introduced an expectation that a name will follow
			if (!ExpectInInvocation("expected argument name", tokens, i + 1, endArgsIndex))
				return false;
		}
	}

	if (returnTypeStart == -1)
	{
		// The type was implicit; blame the "defun"
		output.source.push_back(
		    {"void", StringOutMod_SpaceAfter, &tokens[startTokenIndex], &tokens[startTokenIndex]});
		output.header.push_back(
		    {"void", StringOutMod_SpaceAfter, &tokens[startTokenIndex], &tokens[startTokenIndex]});
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
				return false;
			}
		}

		std::vector<StringOutput> typeOutput;
		std::vector<StringOutput> afterNameOutput;
		// Arrays cannot be return types, they must be * instead
		if (!tokenizedCTypeToString_Recursive(tokens, returnTypeStart,
		                                      /*allowArray=*/false, typeOutput, afterNameOutput))
			return false;

		if (!afterNameOutput.empty())
		{
			const Token* problemToken = afterNameOutput.begin()->startToken;
			ErrorAtToken(*problemToken,
			             "Return types cannot have this type. An error in the code has occurred, "
			             "because the parser shouldn't have gotten this far");
			return false;
		}

		// Functions need a space between type and name; add it
		addModifierToStringOutput(typeOutput.back(), StringOutMod_SpaceAfter);

		output.source.insert(output.source.end(), typeOutput.begin(), typeOutput.end());
		output.header.insert(output.header.end(), typeOutput.begin(), typeOutput.end());
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
		std::vector<StringOutput> typeOutput;
		std::vector<StringOutput> afterNameOutput;
		bool typeValid =
		    tokenizedCTypeToString_Recursive(tokens, arg.startTypeIndex,
		                                     /*allowArray=*/true, typeOutput, afterNameOutput);
		if (!typeValid)
			return false;

		addModifierToStringOutput(typeOutput.back(), StringOutMod_SpaceAfter);

		// Type
		PushBackAll(output.source, typeOutput);
		PushBackAll(output.header, typeOutput);

		// Name
		output.source.push_back({tokens[arg.nameIndex].contents, StringOutMod_ConvertArgumentName,
		                         &tokens[arg.nameIndex], &tokens[arg.nameIndex]});
		output.header.push_back({tokens[arg.nameIndex].contents, StringOutMod_ConvertArgumentName,
		                         &tokens[arg.nameIndex], &tokens[arg.nameIndex]});

		// Array
		PushBackAll(output.source, afterNameOutput);
		PushBackAll(output.header, afterNameOutput);

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

	int startBodyIndex = endArgsIndex + 1;
	output.source.push_back(
	    {"{", StringOutMod_NewlineAfter, &tokens[startBodyIndex], &tokens[startBodyIndex]});

	// Evaluate our body!
	EvaluatorContext bodyContext = context;
	bodyContext.scope = EvaluatorScope_Body;
	int bodyErrors =
	    EvaluateGenerate_Recursive(environment, bodyContext, tokens, startBodyIndex, output);
	if (bodyErrors)
		return false;

	output.source.push_back(
	    {"}", StringOutMod_NewlineAfter, &tokens[endTokenIndex], &tokens[endTokenIndex]});

	// TODO: Populate
	std::vector<FunctionArgumentMetadata> argumentsMetadata;
	output.functions.push_back({nameToken.contents, &tokens[startTokenIndex],
	                            &tokens[endTokenIndex], std::move(argumentsMetadata)});

	return true;
}

//
// Macros
//

// TODO: Only necessary for macro definition generated body
// invocationSource should be a human-generated file, when possible
// void makeMacroSource(char* buffer, int bufferSize, const char* invocationSource)
// {
// 	SafeSnprinf(buffer, bufferSize, "%s.macroexpand", invocationSource);
// }

bool SquareMacro(EvaluatorEnvironment& environment, const EvaluatorContext& context,
                 const std::vector<Token>& tokens, int startTokenIndex, std::vector<Token>& output)
{
	if (IsForbiddenEvaluatorScope("square", tokens[startTokenIndex], context,
	                              EvaluatorScope_Module))
		return false;

	int endInvocationIndex = FindCloseParenTokenIndex(tokens, startTokenIndex);

	const Token& startToken = tokens[startTokenIndex];

	// Skip opening paren of invocation
	int nameTokenIndex = startTokenIndex + 1;

	int startArgsIndex = nameTokenIndex + 1;
	if (!ExpectInInvocation("square expected expression", tokens, startArgsIndex,
	                        endInvocationIndex))
		return false;

	// TODO bad way to retrieve args
	int endArgsIndex = endInvocationIndex;

	// TODO: Source line numbers?
	output.push_back({TokenType_OpenParen, "", startToken.source, startToken.lineNumber,
	                  startToken.columnStart, startToken.columnEnd});
	output.push_back({TokenType_Symbol, "*", startToken.source, startToken.lineNumber,
	                  startToken.columnStart, startToken.columnEnd});

	// Note: this will cause the passed in argument to be evaluated twice
	for (int numTimes = 0; numTimes < 2; ++numTimes)
	{
		for (int i = startArgsIndex; i < endArgsIndex; ++i)
		{
			Token generatedToken = tokens[i];
			// TODO: Add annotations saying it was from macroexpansion?
			output.push_back(generatedToken);
		}
	}

	const Token& endToken = tokens[endInvocationIndex];

	output.push_back({TokenType_CloseParen, "", endToken.source, endToken.lineNumber,
	                  endToken.columnStart, endToken.columnEnd});

	return true;
}

//
// Evaluator
//

// Dispatch to a generator or expand a macro and evaluate its output recursively
bool HandleInvocation_Recursive(EvaluatorEnvironment& environment, const EvaluatorContext& context,
                                const std::vector<Token>& tokens, int invocationStartIndex,
                                GeneratorOutput& output)
{
	const Token& invocationStart = tokens[invocationStartIndex];
	const Token& invocationName = tokens[invocationStartIndex + 1];
	if (!ExpectTokenType("evaluator", invocationName, TokenType_Symbol))
		return false;

	MacroFunc invokedMacro = findMacro(environment, invocationName.contents.c_str());
	if (invokedMacro)
	{
		// We must use a separate vector for each macro because Token lists must be immutable. If
		// they weren't, pointers to tokens would be invalidated
		const std::vector<Token>* macroOutputTokens = nullptr;
		bool macroSucceeded;
		{
			// Do NOT modify token lists after they are created. You can change the token contents
			std::vector<Token>* macroOutputTokensNoConst_CREATIONONLY = new std::vector<Token>();

			// Have the macro generate some code for us!
			macroSucceeded = invokedMacro(environment, context, tokens, invocationStartIndex,
			                              *macroOutputTokensNoConst_CREATIONONLY);

			// Make it const to save any temptation of modifying the list and breaking everything
			macroOutputTokens = macroOutputTokensNoConst_CREATIONONLY;
		}

		// Don't even try to validate the code if the macro wasn't satisfied
		if (!macroSucceeded)
		{
			// Deleting these tokens is only safe at this point because we know we have not
			// evaluated them. As soon as they are evaluated, they must be kept around
			delete macroOutputTokens;
			return false;
		}

		// TODO: Pretty print to macro expand file and change output token source to
		// point there

		// Macro must generate valid parentheses pairs!
		bool validateResult = validateParentheses(*macroOutputTokens);
		if (!validateResult)
		{
			NoteAtToken(invocationStart,
			            "Code was generated from macro. See erroneous macro "
			            "expansion below:");
			printTokens(*macroOutputTokens);
			printf("\n");
			// Deleting these tokens is only safe at this point because we know we have not
			// evaluated them. As soon as they are evaluated, they must be kept around
			delete macroOutputTokens;
			return false;
		}

		// Macro succeeded and output valid tokens. Keep its tokens for later referencing and
		// destruction. Note that macroOutputTokens cannot be destroyed safely until all pointers to
		// its Tokens are cleared. This means even if we fail while evaluating the tokens, we will
		// keep the array around because the environment might still hold references to the tokens.
		// It's also necessary for error reporting
		environment.macroExpansions.push_back(macroOutputTokens);

		// TODO: Have macro output to regular output. Only macro and generator
		// definitions need to go to intermediate files
		GeneratorOutput macroOutput;
		// Note that macros always inherit the current context, whereas bodies change it
		int result = EvaluateGenerate_Recursive(environment, context, *macroOutputTokens,
		                                        /*startTokenIndex=*/0, macroOutput);
		if (result != 0)
		{
			NoteAtToken(invocationStart,
			            "Code was generated from macro. See macro expansion below:");
			printTokens(*macroOutputTokens);
			printf("\n");
			return false;
		}

		// TODO Remove, debug only
		NameStyleSettings nameSettings;
		printGeneratorOutput(macroOutput, nameSettings);
	}
	else
	{
		GeneratorFunc invokedGenerator =
		    findGenerator(environment, invocationName.contents.c_str());
		if (invokedGenerator)
		{
			return invokedGenerator(environment, context, tokens, invocationStartIndex, output);
		}
		else if (context.scope == EvaluatorScope_Module)
		{
			ErrorAtTokenf(invocationStart,
			              "Unknown function %s. Only macros and generators may be "
			              "invoked at top level",
			              invocationName.contents.c_str());
			return false;
		}
		else
		{
			// TODO: Fallback to C, C++, and (generated) Cakelisp function invocation generator
			return true;
		}
	}

	return true;
}

int EvaluateGenerate_Recursive(EvaluatorEnvironment& environment, const EvaluatorContext& context,
                               const std::vector<Token>& tokens, int startTokenIndex,
                               GeneratorOutput& output)
{
	// Note that in most cases, we will continue evaluation in order to turn up more errors
	int numErrors = 0;

	int numTokens = tokens.size();
	for (int currentTokenIndex = startTokenIndex; currentTokenIndex < numTokens;
	     ++currentTokenIndex)
	{
		const Token& token = tokens[currentTokenIndex];

		if (token.type == TokenType_OpenParen)
		{
			// Invocation of a macro, generator, or function (either foreign or Cakelisp function)
			bool invocationSucceeded =
			    HandleInvocation_Recursive(environment, context, tokens, currentTokenIndex, output);
			if (!invocationSucceeded)
				++numErrors;

			// Skip invocation body. for()'s increment will skip us past the final ')'
			currentTokenIndex = FindCloseParenTokenIndex(tokens, currentTokenIndex);
		}
		else if (token.type == TokenType_CloseParen)
		{
			// This is totally normal. We've reached the end of the body or file. If that isn't the
			// case, the code isn't being validated with validateParentheses(); code which hasn't
			// been validated should NOT be run - this function trusts its inputs blindly!
			// This will also be hit if eval itself has been broken: it is expected to skip tokens
			// within invocations, including the final close paren
			return numErrors;
		}
		else
		{
			// The remaining token types evaluate to themselves. Output them directly.
			if (ExpectEvaluatorScope("evaluated constant", token, context,
			                         EvaluatorScope_ExpressionsOnly))
			{
				switch (token.type)
				{
					case TokenType_Symbol:
						output.source.push_back(
						    {token.contents, StringOutMod_None, &token, &token});
						break;
					case TokenType_String:
						output.source.push_back(
						    {token.contents, StringOutMod_SurroundWithQuotes, &token, &token});
						break;
					default:
						ErrorAtTokenf(
						    token,
						    "Unhandled token type %s; has a new token type been added, or "
						    "evaluator has been changed?",
						    tokenTypeToString(token.type));
						return 1;
				}
			}
			else
				numErrors++;
		}
	}

	return numErrors;
}

// This serves only as a warning. I want to be very explicit with the lifetime of tokens
EvaluatorEnvironment::~EvaluatorEnvironment()
{
	if (!macroExpansions.empty())
	{
		printf(
		    "Warning: environmentDestroyMacroExpansionsInvalidateTokens() has not been called. "
		    "This will leak memory.\n Call it once you are certain no tokens in any expansions "
		    "will be referenced.\n");
	}
}

void environmentDestroyMacroExpansionsInvalidateTokens(EvaluatorEnvironment& environment)
{
	for (const std::vector<Token>* macroExpansion : environment.macroExpansions)
		delete macroExpansion;
	environment.macroExpansions.clear();
}

const char* importLanguageToString(ImportLanguage type)
{
	switch (type)
	{
		case ImportLanguage_None:
			return "None";
		case ImportLanguage_C:
			return "C";
		case ImportLanguage_Cakelisp:
			return "Cakelisp";
		default:
			return "Unknown";
	}
}

// TODO This should be automatically generated
NameStyleMode getNameStyleModeForFlags(const NameStyleSettings& settings,
                                       StringOutputModifierFlags modifierFlags)
{
	NameStyleMode mode = NameStyleMode_None;
	int numMatchingFlags = 0;
	if (modifierFlags & StringOutMod_ConvertTypeName)
	{
		++numMatchingFlags;
		mode = settings.typeNameMode;

		static bool hasWarned = false;
		if (!hasWarned && mode == NameStyleMode_PascalCase)
		{
			hasWarned = true;
			printf(
			    "\nWarning: Use of PascalCase for type names is discouraged because it will "
			    "destroy lowercase C type names. You should use PascalCaseIfPlural instead, which "
			    "will only apply case changes if the name looks lisp-y. This warning will only "
			    "appear once.\n");
		}
	}
	if (modifierFlags & StringOutMod_ConvertFunctionName)
	{
		++numMatchingFlags;
		mode = settings.functionNameMode;
	}
	if (modifierFlags & StringOutMod_ConvertArgumentName)
	{
		++numMatchingFlags;
		mode = settings.argumentNameMode;
	}
	if (modifierFlags & StringOutMod_ConvertVariableName)
	{
		++numMatchingFlags;
		mode = settings.variableNameMode;
	}
	if (modifierFlags & StringOutMod_ConvertGlobalVariableName)
	{
		++numMatchingFlags;
		mode = settings.globalVariableNameMode;
	}

	if (numMatchingFlags > 1)
		printf("\nWarning: Name was given conflicting convert flags: %d\n", (int)modifierFlags);

	return mode;
}

void debugPrintStringOutput(const NameStyleSettings& settings, const StringOutput& outputOperation)
{
	NameStyleMode mode = getNameStyleModeForFlags(settings, outputOperation.modifiers);
	if (mode)
	{
		char convertedName[MAX_NAME_LENGTH] = {0};
		lispNameStyleToCNameStyle(mode, outputOperation.output.c_str(), convertedName,
		                          sizeof(convertedName));
		printf("%s", convertedName);
	}
	else if (outputOperation.modifiers & StringOutMod_SurroundWithQuotes)
		printf("\"%s\"", outputOperation.output.c_str());
	else
		printf("%s", outputOperation.output.c_str());

	if (outputOperation.modifiers & StringOutMod_SpaceAfter)
		printf(" ");
	if (outputOperation.modifiers & StringOutMod_NewlineAfter)
		printf("\n");
}

//
// Debugging
//
void printGeneratorOutput(const GeneratorOutput& generatedOutput,
                          const NameStyleSettings& nameSettings)
{
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
		printf("%s\t(%s)\n", import.importName.c_str(), importLanguageToString(import.language));
	}

	printf("\n\tFunctions:\n");
	for (const FunctionMetadata& function : generatedOutput.functions)
	{
		printf("%s\n", function.name.c_str());
	}
}
