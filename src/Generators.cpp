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
		else if (typeInvocation.contents.compare("&&") == 0 ||
		         typeInvocation.contents.compare("rval-ref-to") == 0)
		{
			int typeIndex =
			    getExpectedArgument("expected type", tokens, startTokenIndex, 1, endTokenIndex);
			if (typeIndex == -1)
				return false;

			if (!tokenizedCTypeToString_Recursive(tokens, typeIndex, allowArray, typeOutput,
			                                      afterNameOutput))
				return false;

			typeOutput.push_back({"&&", StringOutMod_None, &typeInvocation, &typeInvocation});
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

	bool isModuleLocal = tokens[startTokenIndex + 1].contents.compare("defun-local") == 0;

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

	// TODO: Hot-reloading functions shouldn't be declared static, right?
	if (isModuleLocal)
		output.source.push_back({"static", StringOutMod_SpaceAfter, &tokens[startTokenIndex],
		                         &tokens[startTokenIndex]});

	if (returnTypeStart == -1)
	{
		// The type was implicit; blame the "defun"
		output.source.push_back(
		    {"void", StringOutMod_SpaceAfter, &tokens[startTokenIndex], &tokens[startTokenIndex]});
		if (!isModuleLocal)
			output.header.push_back({"void", StringOutMod_SpaceAfter, &tokens[startTokenIndex],
			                         &tokens[startTokenIndex]});
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
		if (!isModuleLocal)
			output.header.insert(output.header.end(), typeOutput.begin(), typeOutput.end());
	}

	output.source.push_back(
	    {nameToken.contents, StringOutMod_ConvertFunctionName, &nameToken, &nameToken});
	if (!isModuleLocal)
		output.header.push_back(
		    {nameToken.contents, StringOutMod_ConvertFunctionName, &nameToken, &nameToken});

	output.source.push_back({EmptyString, StringOutMod_OpenParen, &argsStart, &argsStart});
	if (!isModuleLocal)
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
		if (!isModuleLocal)
			PushBackAll(output.header, typeOutput);

		// Name
		output.source.push_back({tokens[arg.nameIndex].contents, StringOutMod_ConvertArgumentName,
		                         &tokens[arg.nameIndex], &tokens[arg.nameIndex]});
		if (!isModuleLocal)
			output.header.push_back({tokens[arg.nameIndex].contents,
			                         StringOutMod_ConvertArgumentName, &tokens[arg.nameIndex],
			                         &tokens[arg.nameIndex]});

		// Array
		PushBackAll(output.source, afterNameOutput);
		if (!isModuleLocal)
			PushBackAll(output.header, afterNameOutput);

		if (i + 1 < numFunctionArguments)
		{
			// It's a little weird to have to pick who is responsible for the comma. In this case
			// both the name and the type of the next arg are responsible, because there wouldn't be
			// a comma if there wasn't a next arg
			DefunArgument& nextArg = arguments[i + 1];
			output.source.push_back({EmptyString, StringOutMod_ListSeparator,
			                         &tokens[arg.nameIndex], &tokens[nextArg.startTypeIndex]});
			if (!isModuleLocal)
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
	if (!isModuleLocal)
	{
		output.header.push_back(
		    {EmptyString, StringOutMod_CloseParen, &tokens[endArgsIndex], &tokens[endArgsIndex]});
		// Forward declarations end with ;
		output.header.push_back(
		    {EmptyString, StringOutMod_EndStatement, &tokens[endArgsIndex], &tokens[endArgsIndex]});
	}

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
	// We can't expect any scope because C preprocessor macros can be called in any scope
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

// Handles both uninitized and initilized variables as well as global and static
bool VariableDeclarationGenerator(EvaluatorEnvironment& environment,
                                  const EvaluatorContext& context, const std::vector<Token>& tokens,
                                  int startTokenIndex, GeneratorOutput& output)
{
	if (IsForbiddenEvaluatorScope("variable declaration", tokens[startTokenIndex], context,
	                              EvaluatorScope_ExpressionsOnly))
		return false;

	int nameTokenIndex = startTokenIndex + 1;
	const Token& funcNameToken = tokens[nameTokenIndex];

	int endInvocationIndex = FindCloseParenTokenIndex(tokens, startTokenIndex);

	// Global variables will get extern'd in the header
	bool isGlobal = funcNameToken.contents.compare("global-var") == 0;
	if (isGlobal && !ExpectEvaluatorScope("global variable declaration", tokens[startTokenIndex],
	                                      context, EvaluatorScope_Module))
		return false;

	// Only necessary for static variables declared inside a function
	bool isStatic = funcNameToken.contents.compare("static-var") == 0;
	if (isStatic && !ExpectEvaluatorScope("static variable declaration", tokens[startTokenIndex],
	                                      context, EvaluatorScope_Body))
		return false;

	int typeIndex = getExpectedArgument("expected variable type", tokens, startTokenIndex, 1,
	                                    endInvocationIndex);
	if (typeIndex == -1)
		return false;

	int varNameIndex = getExpectedArgument("expected variable name", tokens, startTokenIndex, 2,
	                                       endInvocationIndex);
	if (varNameIndex == -1)
		return false;

	std::vector<StringOutput> typeOutput;
	std::vector<StringOutput> typeAfterNameOutput;
	// Arrays cannot be return types, they must be * instead
	if (!tokenizedCTypeToString_Recursive(tokens, typeIndex,
	                                      /*allowArray=*/true, typeOutput, typeAfterNameOutput))
		return false;

	// At this point, we probably have a valid variable. Start outputting
	addModifierToStringOutput(typeOutput.back(), StringOutMod_SpaceAfter);

	if (isGlobal)
		output.header.push_back({"extern", StringOutMod_SpaceAfter, &tokens[startTokenIndex],
		                         &tokens[startTokenIndex]});
	// else because no variable may be declared extern while also being static
	// Automatically make module-declared variables static, reserving "static-var" for functions
	else if (isStatic || context.scope == EvaluatorScope_Module)
		output.source.push_back({"static", StringOutMod_SpaceAfter, &tokens[startTokenIndex],
		                         &tokens[startTokenIndex]});

	// Type
	PushBackAll(output.source, typeOutput);
	if (isGlobal)
		PushBackAll(output.header, typeOutput);

	// Name
	output.source.push_back({tokens[varNameIndex].contents, StringOutMod_ConvertArgumentName,
	                         &tokens[varNameIndex], &tokens[varNameIndex]});
	if (isGlobal)
		output.header.push_back({tokens[varNameIndex].contents, StringOutMod_ConvertArgumentName,
		                         &tokens[varNameIndex], &tokens[varNameIndex]});

	// Array
	PushBackAll(output.source, typeAfterNameOutput);
	if (isGlobal)
		PushBackAll(output.header, typeAfterNameOutput);

	// Possibly find whether it is initialized
	int valueIndex = varNameIndex + 1;
	if (tokens[varNameIndex].type == TokenType_OpenParen)
	{
		// Skip any nesting
		valueIndex = FindCloseParenTokenIndex(tokens, varNameIndex);
	}

	// Initialized
	if (valueIndex < endInvocationIndex)
	{
		output.source.push_back(
		    {EmptyString, StringOutMod_SpaceAfter, &tokens[valueIndex], &tokens[valueIndex]});
		output.source.push_back(
		    {"=", StringOutMod_SpaceAfter, &tokens[valueIndex], &tokens[valueIndex]});

		EvaluatorContext expressionContext = context;
		expressionContext.scope = EvaluatorScope_ExpressionsOnly;
		if (EvaluateGenerate_Recursive(environment, expressionContext, tokens, valueIndex,
		                               output) != 0)
			return false;
	}

	output.source.push_back({EmptyString, StringOutMod_EndStatement, &tokens[endInvocationIndex],
	                         &tokens[endInvocationIndex]});
	if (isGlobal)
		output.header.push_back({EmptyString, StringOutMod_EndStatement,
		                         &tokens[endInvocationIndex], &tokens[endInvocationIndex]});

	return true;
}

bool ArrayAccessGenerator(EvaluatorEnvironment& environment, const EvaluatorContext& context,
                          const std::vector<Token>& tokens, int startTokenIndex,
                          GeneratorOutput& output)
{
	// This doesn't mean (at) can't be an lvalue
	if (!ExpectEvaluatorScope("at", tokens[startTokenIndex], context,
	                          EvaluatorScope_ExpressionsOnly))
		return false;

	int endInvocationIndex = FindCloseParenTokenIndex(tokens, startTokenIndex);

	int firstOffsetIndex =
	    getExpectedArgument("expected offset", tokens, startTokenIndex, 1, endInvocationIndex);
	if (firstOffsetIndex == -1)
		return false;

	std::vector<int> offsetTokenIndices;
	int arrayNameIndex = -1;
	for (int i = firstOffsetIndex; i < endInvocationIndex; ++i)
	{
		const Token& token = tokens[i];

		int endOfArgument = i;
		if (token.type == TokenType_OpenParen)
			endOfArgument = FindCloseParenTokenIndex(tokens, i);

		// We hit the final argument, which should be the array to access
		if (endOfArgument + 1 == endInvocationIndex)
		{
			arrayNameIndex = i;
			break;
		}

		offsetTokenIndices.push_back(i);
		if (token.type == TokenType_OpenParen)
		{
			// Skip any nesting
			i = endOfArgument;
		}
	}

	if (offsetTokenIndices.empty() || arrayNameIndex == -1)
	{
		ErrorAtToken(tokens[firstOffsetIndex],
		             "expected at least one offset and an array to offset into");
		return false;
	}

	// Evaluate array, which could be an arbitrarily complex expression
	EvaluatorContext expressionContext = context;
	expressionContext.scope = EvaluatorScope_ExpressionsOnly;
	if (EvaluateGenerate_Recursive(environment, expressionContext, tokens, arrayNameIndex,
	                               output) != 0)
		return false;

	for (int offsetTokenIndex : offsetTokenIndices)
	{
		output.source.push_back(
		    {"[", StringOutMod_None, &tokens[offsetTokenIndex], &tokens[offsetTokenIndex]});
		if (EvaluateGenerate_Recursive(environment, expressionContext, tokens, offsetTokenIndex,
		                               output) != 0)
			return false;
		output.source.push_back(
		    {"]", StringOutMod_None, &tokens[offsetTokenIndex], &tokens[offsetTokenIndex]});
	}

	return true;
}

enum CStatementOperationType
{
	// Insert keywordOrSymbol between each thing
	Splice,

	OpenParen,
	CloseParen,
	OpenBlock,
	CloseBlock,
	OpenList,
	CloseList,

	Keyword,
	EndStatement,

	// Evaluate argument(s)
	Expression,
	ExpressionList,
	// Body will read the remaining arguments; argumentIndex will tell it where to start
	Body,
};
struct CStatementOperation
{
	CStatementOperationType type;
	const char* keywordOrSymbol;
	// 0 = operation name
	// 1 = first argument to operation (etc.)
	int argumentIndex;
};

bool cStatementOutput(EvaluatorEnvironment& environment, const EvaluatorContext& context,
                      const std::vector<Token>& tokens, int startTokenIndex,
                      const CStatementOperation* operation, int numOperations,
                      GeneratorOutput& output)
{
	// TODO: Add expects for scope
	int endTokenIndex = FindCloseParenTokenIndex(tokens, startTokenIndex);
	int nameTokenIndex = startTokenIndex + 1;
	// int startArgsIndex = nameTokenIndex + 1;
	const Token& nameToken = tokens[nameTokenIndex];
	for (int i = 0; i < numOperations; ++i)
	{
		switch (operation[i].type)
		{
			case Keyword:
				output.source.push_back({operation[i].keywordOrSymbol, StringOutMod_SpaceAfter,
				                         &nameToken, &nameToken});
				break;
			case Splice:
			{
				if (operation[i].argumentIndex < 0)
				{
					printf("Error: Expected valid argument index for start of splice list\n");
					return false;
				}
				int startSpliceListIndex =
				    getExpectedArgument("expected expressions", tokens, startTokenIndex,
				                        operation[i].argumentIndex, endTokenIndex);
				if (startSpliceListIndex == -1)
					return false;
				EvaluatorContext bodyContext = context;
				bodyContext.scope = EvaluatorScope_ExpressionsOnly;
				StringOutput spliceDelimiterTemplate = {operation[i].keywordOrSymbol,
				                                        StringOutMod_SpaceAfter, nullptr, nullptr};
				int numErrors = EvaluateGenerateAll_Recursive(environment, bodyContext, tokens,
				                                              startSpliceListIndex,
				                                              spliceDelimiterTemplate, output);
				if (numErrors)
					return false;
				break;
			}
			case OpenParen:
				output.source.push_back(
				    {EmptyString, StringOutMod_OpenParen, &nameToken, &nameToken});
				break;
			case CloseParen:
				output.source.push_back(
				    {EmptyString, StringOutMod_CloseParen, &nameToken, &nameToken});
				break;
			case OpenBlock:
				output.source.push_back(
				    {EmptyString, StringOutMod_OpenBlock, &nameToken, &nameToken});
				break;
			case CloseBlock:
				output.source.push_back(
				    {EmptyString, StringOutMod_CloseBlock, &nameToken, &nameToken});
				break;
			case OpenList:
				output.source.push_back(
				    {EmptyString, StringOutMod_OpenList, &nameToken, &nameToken});
				break;
			case CloseList:
				output.source.push_back(
				    {EmptyString, StringOutMod_CloseList, &nameToken, &nameToken});
				break;
			case EndStatement:
				output.source.push_back(
				    {EmptyString, StringOutMod_EndStatement, &nameToken, &nameToken});
				break;
			case Expression:
			{
				if (operation[i].argumentIndex < 0)
				{
					printf("Error: Expected valid argument index for expression\n");
					return false;
				}
				int startExpressionIndex =
				    getExpectedArgument("expected expression", tokens, startTokenIndex,
				                        operation[i].argumentIndex, endTokenIndex);
				if (startExpressionIndex == -1)
					return false;
				EvaluatorContext expressionContext = context;
				expressionContext.scope = EvaluatorScope_ExpressionsOnly;
				if (EvaluateGenerate_Recursive(environment, expressionContext, tokens,
				                               startExpressionIndex, output) != 0)
					return false;
				break;
			}
			case ExpressionList:
			{
				if (operation[i].argumentIndex < 0)
				{
					printf("Error: Expected valid argument index for expression\n");
					return false;
				}
				int startExpressionIndex =
				    getExpectedArgument("expected expression", tokens, startTokenIndex,
				                        operation[i].argumentIndex, endTokenIndex);
				if (startExpressionIndex == -1)
					return false;
				EvaluatorContext expressionContext = context;
				expressionContext.scope = EvaluatorScope_ExpressionsOnly;
				StringOutput listDelimiterTemplate = {EmptyString, StringOutMod_ListSeparator,
				                                      nullptr, nullptr};
				if (EvaluateGenerateAll_Recursive(environment, expressionContext, tokens,
				                                  startExpressionIndex, listDelimiterTemplate,
				                                  output) != 0)
					return false;
				break;
			}
			case Body:
			{
				if (operation[i].argumentIndex < 0)
				{
					printf("Error: Expected valid argument index for body\n");
					return false;
				}
				int startBodyIndex = getExpectedArgument("expected body", tokens, startTokenIndex,
				                                         operation[i].argumentIndex, endTokenIndex);
				if (startBodyIndex == -1)
					return false;
				EvaluatorContext bodyContext = context;
				bodyContext.scope = EvaluatorScope_Body;
				// The statements will need to handle their ;
				// TODO Remove delimiter, we don't need it
				StringOutput bodyDelimiterTemplate = {EmptyString, StringOutMod_None, nullptr,
				                                      nullptr};
				int numErrors =
				    EvaluateGenerateAll_Recursive(environment, bodyContext, tokens, startBodyIndex,
				                                  bodyDelimiterTemplate, output);
				if (numErrors)
					return false;
				break;
			}
			default:
				printf("Output type not handled\n");
				return false;
		}
	}

	return true;
}

// This generator handles several C/C++ constructs by specializing on the invocation name
// We can handle most of them, but some (if-else chains, switch, for) require extra attention
bool CStatementGenerator(EvaluatorEnvironment& environment, const EvaluatorContext& context,
                         const std::vector<Token>& tokens, int startTokenIndex,
                         GeneratorOutput& output)
{
	int nameTokenIndex = startTokenIndex + 1;
	const Token& nameToken = tokens[nameTokenIndex];

	// Loops
	const CStatementOperation whileStatement[] = {
	    {Keyword, "while", -1},    {OpenParen, nullptr, -1}, {Expression, nullptr, 1},
	    {CloseParen, nullptr, -1}, {OpenBlock, nullptr, -1}, {Body, nullptr, 2},
	    {CloseBlock, nullptr, -1}};

	// Conditionals
	const CStatementOperation whenStatement[] = {
	    {Keyword, "if", -1},       {OpenParen, nullptr, -1}, {Expression, nullptr, 1},
	    {CloseParen, nullptr, -1}, {OpenBlock, nullptr, -1}, {Body, nullptr, 2},
	    {CloseBlock, nullptr, -1}};

	// Misc.
	const CStatementOperation returnStatement[] = {
	    {Keyword, "return", -1}, {Expression, nullptr, 1}, {EndStatement, nullptr, -1}};

	const CStatementOperation initializerList[] = {
	    {OpenList, nullptr, -1}, {ExpressionList, nullptr, 1}, {CloseList, nullptr, -1}};

	const CStatementOperation assignmentStatement[] = {{Expression /*Name*/, nullptr, 1},
	                                                   {Keyword, "=", -1},
	                                                   {Expression, nullptr, 2},
	                                                   {EndStatement, nullptr, -1}};

	const CStatementOperation dereference[] = {{Keyword, "*", -1}, {Expression, nullptr, 1}};
	const CStatementOperation addressOf[] = {{Keyword, "&", -1}, {Expression, nullptr, 1}};

	// Similar to progn, but doesn't necessarily mean things run in order (this doesn't add barriers
	// or anything). It's useful both for making arbitrary scopes and for making if blocks with
	// multiple statements
	const CStatementOperation blockStatement[] = {
	    {OpenBlock, nullptr, -1}, {Body, nullptr, 1}, {CloseBlock, nullptr, -1}};

	// https://www.tutorialspoint.com/cprogramming/c_operators.htm proved useful
	const CStatementOperation booleanOr[] = {{Splice, "||", 1}};
	const CStatementOperation booleanAnd[] = {{Splice, "&&", 1}};
	const CStatementOperation booleanNot[] = {{Keyword, "!", -1}, {Expression, nullptr, 1}};

	const CStatementOperation bitwiseOr[] = {{Splice, "|", 1}};
	const CStatementOperation bitwiseAnd[] = {{Splice, "&", 1}};
	const CStatementOperation bitwiseXOr[] = {{Splice, "&", 1}};
	const CStatementOperation bitwiseOnesComplement[] = {{Keyword, "~", -1},
	                                                     {Expression, nullptr, 1}};
	const CStatementOperation bitwiseLeftShift[] = {{Splice, "<<", 1}};
	const CStatementOperation bitwiseRightShift[] = {{Splice, ">>", 1}};

	const CStatementOperation relationalEquality[] = {{Splice, "==", 1}};
	const CStatementOperation relationalNotEqual[] = {{Splice, "!=", 1}};
	const CStatementOperation relationalLessThanEqual[] = {{Splice, "<=", 1}};
	const CStatementOperation relationalGreaterThanEqual[] = {{Splice, ">=", 1}};
	const CStatementOperation relationalLessThan[] = {{Splice, "<", 1}};
	const CStatementOperation relationalGreaterThan[] = {{Splice, ">", 1}};

	const CStatementOperation add[] = {{Splice, "+", 1}};
	const CStatementOperation subtract[] = {{Splice, "-", 1}};
	const CStatementOperation multiply[] = {{Splice, "*", 1}};
	const CStatementOperation divide[] = {{Splice, "/", 1}};
	const CStatementOperation modulus[] = {{Splice, "%", 1}};
	// Always pre-increment, which matches what you'd expect given the invocation comes before the
	// expression. It's also slightly faster, yadda yadda
	const CStatementOperation increment[] = {{Keyword, "++", -1}, {Expression, nullptr, 1}};
	const CStatementOperation decrement[] = {{Keyword, "--", -1}, {Expression, nullptr, 1}};

	// Useful for marking e.g. the increment statement in a for loop blank
	// const CStatementOperation noOpStatement[] = {};

	const struct
	{
		const char* name;
		const CStatementOperation* operations;
		int numOperations;
	} statementOperators[] = {
	    {"while", whileStatement, ArraySize(whileStatement)},
	    {"return", returnStatement, ArraySize(returnStatement)},
	    {"when", whenStatement, ArraySize(whenStatement)},
	    {"array", initializerList, ArraySize(initializerList)},
	    {"set", assignmentStatement, ArraySize(assignmentStatement)},
	    // Calling it scope so Emacs will auto-format correctly
	    // TODO: I like "block" better. How do I change the formatter to not be confusing?
	    {"scope", blockStatement, ArraySize(blockStatement)},
	    // Pointers
	    {"deref", dereference, ArraySize(dereference)},
	    {"addr", addressOf, ArraySize(addressOf)},
	    // Expressions
	    {"or", booleanOr, ArraySize(booleanOr)},
	    {"and", booleanAnd, ArraySize(booleanAnd)},
	    {"not", booleanNot, ArraySize(booleanNot)},
	    {"bit-or", bitwiseOr, ArraySize(bitwiseOr)},
	    {"bit-and", bitwiseAnd, ArraySize(bitwiseAnd)},
	    {"bit-xor", bitwiseXOr, ArraySize(bitwiseXOr)},
	    {"bit-ones-complement", bitwiseOnesComplement, ArraySize(bitwiseOnesComplement)},
	    {"bit-<<", bitwiseLeftShift, ArraySize(bitwiseLeftShift)},
	    {"bit->>", bitwiseRightShift, ArraySize(bitwiseRightShift)},
	    {"=", relationalEquality, ArraySize(relationalEquality)},
	    {"!=", relationalNotEqual, ArraySize(relationalNotEqual)},
	    {"eq", relationalEquality, ArraySize(relationalEquality)},
	    {"neq", relationalNotEqual, ArraySize(relationalNotEqual)},
	    {"<=", relationalLessThanEqual, ArraySize(relationalLessThanEqual)},
	    {">=", relationalGreaterThanEqual, ArraySize(relationalGreaterThanEqual)},
	    {"<", relationalLessThan, ArraySize(relationalLessThan)},
	    {">", relationalGreaterThan, ArraySize(relationalGreaterThan)},
	    // Arithmetic
	    {"+", add, ArraySize(add)},
	    {"-", subtract, ArraySize(subtract)},
	    {"*", multiply, ArraySize(multiply)},
	    {"/", divide, ArraySize(divide)},
	    {"%", modulus, ArraySize(modulus)},
	    {"mod", modulus, ArraySize(modulus)},
	    {"++", increment, ArraySize(increment)},
	    {"--", decrement, ArraySize(decrement)},
	};

	for (unsigned int i = 0; i < ArraySize(statementOperators); ++i)
	{
		if (nameToken.contents.compare(statementOperators[i].name) == 0)
		{
			return cStatementOutput(environment, context, tokens, startTokenIndex,
			                        statementOperators[i].operations,
			                        statementOperators[i].numOperations, output);
		}
	}

	ErrorAtToken(nameToken, "C statement generator received unrecognized keyword");
	return false;
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

//
// Environment interaction
//
void importFundamentalGenerators(EvaluatorEnvironment& environment)
{
	environment.generators["c-import"] = CImportGenerator;

	environment.generators["defun"] = DefunGenerator;
	environment.generators["defun-local"] = DefunGenerator;

	environment.generators["var"] = VariableDeclarationGenerator;
	environment.generators["global-var"] = VariableDeclarationGenerator;
	environment.generators["static-var"] = VariableDeclarationGenerator;

	environment.generators["at"] = ArrayAccessGenerator;
	environment.generators["nth"] = ArrayAccessGenerator;

	// Dispatches based on invocation name
	const char* cStatementKeywords[] = {
	    "while",
	    "return",
	    "when",
	    "array",
	    "set",
	    "scope",
	    // Pointers
	    "deref",
	    "addr",
	    // Boolean
	    "or",
	    "and",
	    "not",
	    // Bitwise
	    "bit-or",
	    "bit-and",
	    "bit-xor",
	    "bit-ones-complement",
	    "bit-<<",
	    "bit->>",
	    // Relational
	    "=",
	    "!=",
	    "eq",
	    "neq",
	    "<=",
	    ">=",
	    "<",
	    ">",
	    // Arithmetic
	    "+",
	    "-",
	    "*",
	    "/",
	    "%",
	    "mod",
	    "++",
	    "--",
	};
	for (size_t i = 0; i < ArraySize(cStatementKeywords); ++i)
	{
		environment.generators[cStatementKeywords[i]] = CStatementGenerator;
	}
}
