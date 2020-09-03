#include "Generators.hpp"

#include "Evaluator.hpp"
#include "GeneratorHelpers.hpp"
#include "Tokenizer.hpp"
#include "Utilities.hpp"

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
					typeOutput.push_back({EmptyString, StringOutMod_ListSeparator,
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
	if (!ExpectInInvocation("defun expected arguments", tokens, argsIndex, endDefunTokenIndex))
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

	output.source.push_back({EmptyString, StringOutMod_OpenParen, &argsStart, &argsStart});
	output.header.push_back({EmptyString, StringOutMod_OpenParen, &argsStart, &argsStart});

	std::vector<FunctionArgumentMetadata> argumentsMetadata;

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
			output.source.push_back({EmptyString, StringOutMod_ListSeparator,
			                         &tokens[arg.nameIndex], &tokens[nextArg.startTypeIndex]});
			output.header.push_back({EmptyString, StringOutMod_ListSeparator,
			                         &tokens[arg.nameIndex], &tokens[nextArg.startTypeIndex]});
		}

		// Argument metadata
		{
			const Token* startTypeToken = &tokens[arg.startTypeIndex];
			const Token* endTypeToken = startTypeToken;
			if (startTypeToken->type == TokenType_OpenParen)
				endTypeToken = &tokens[FindCloseParenTokenIndex(tokens, arg.startTypeIndex)];

			argumentsMetadata.push_back(
			    {tokens[arg.nameIndex].contents, startTypeToken, endTypeToken});
		}
	}

	output.source.push_back(
	    {EmptyString, StringOutMod_CloseParen, &tokens[endArgsIndex], &tokens[endArgsIndex]});
	output.header.push_back(
	    {EmptyString, StringOutMod_CloseParen, &tokens[endArgsIndex], &tokens[endArgsIndex]});
	// Forward declarations end with ;
	output.header.push_back(
	    {EmptyString, StringOutMod_EndStatement, &tokens[endArgsIndex], &tokens[endArgsIndex]});

	int startBodyIndex = endArgsIndex + 1;
	output.source.push_back(
	    {EmptyString, StringOutMod_OpenBlock, &tokens[startBodyIndex], &tokens[startBodyIndex]});

	// Evaluate our body!
	EvaluatorContext bodyContext = context;
	bodyContext.scope = EvaluatorScope_Body;
	// The statements will need to handle their ;
	// TODO Remove this, we don't need it any more
	StringOutput bodyDelimiterTemplate = {EmptyString, StringOutMod_None, nullptr, nullptr};
	int numErrors = EvaluateGenerateAll_Recursive(environment, bodyContext, tokens, startBodyIndex,
	                                              bodyDelimiterTemplate, output);
	if (numErrors)
		return false;

	output.source.push_back(
	    {EmptyString, StringOutMod_CloseBlock, &tokens[endTokenIndex], &tokens[endTokenIndex]});

	output.functions.push_back({nameToken.contents, &tokens[startTokenIndex],
	                            &tokens[endTokenIndex], std::move(argumentsMetadata)});

	return true;
}

// Surprisingly simple: slap in the name, open parens, then eval arguments one by one and
// comma-delimit them. This is for non-hot-reloadable functions (static invocation)
bool FunctionInvocationGenerator(EvaluatorEnvironment& environment, const EvaluatorContext& context,
                                 const std::vector<Token>& tokens, int startTokenIndex,
                                 GeneratorOutput& output)
{
	// Skip opening paren
	int nameTokenIndex = startTokenIndex + 1;
	const Token& funcNameToken = tokens[nameTokenIndex];
	int endInvocationIndex = FindCloseParenTokenIndex(tokens, startTokenIndex);

	output.source.push_back(
	    {funcNameToken.contents, StringOutMod_ConvertFunctionName, &funcNameToken, &funcNameToken});
	output.source.push_back({EmptyString, StringOutMod_OpenParen, &funcNameToken, &funcNameToken});

	// Arguments
	int startArgsIndex = nameTokenIndex + 1;

	// Function invocations evaluate their arguments
	EvaluatorContext functionInvokeContext = context;
	functionInvokeContext.scope = EvaluatorScope_ExpressionsOnly;
	StringOutput argumentDelimiterTemplate = {EmptyString, StringOutMod_ListSeparator, nullptr,
	                                          nullptr};
	int numErrors =
	    EvaluateGenerateAll_Recursive(environment, functionInvokeContext, tokens, startArgsIndex,
	                                  argumentDelimiterTemplate, output);
	if (numErrors)
		return false;

	output.source.push_back({EmptyString, StringOutMod_CloseParen, &tokens[endInvocationIndex],
	                         &tokens[endInvocationIndex]});
	if (context.scope != EvaluatorScope_ExpressionsOnly)
		output.source.push_back({EmptyString, StringOutMod_EndStatement,
		                         &tokens[endInvocationIndex], &tokens[endInvocationIndex]});

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
	output.push_back({TokenType_OpenParen, EmptyString, startToken.source, startToken.lineNumber,
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

	output.push_back({TokenType_CloseParen, EmptyString, endToken.source, endToken.lineNumber,
	                  endToken.columnStart, endToken.columnEnd});

	return true;
}
