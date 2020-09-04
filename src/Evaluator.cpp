#include "Evaluator.hpp"

#include "Converters.hpp"
#include "Generators.hpp"
#include "GeneratorHelpers.hpp"
#include "Tokenizer.hpp"
#include "Utilities.hpp"
#include "Writer.hpp"

// TODO: safe version of strcat
#include <stdio.h>
#include <string.h>

//
// Environment
//

GeneratorFunc findGenerator(EvaluatorEnvironment& environment, const char* functionName)
{
	// For testing only: Lazy-initialize the bootstrapping/fundamental generators
	if (environment.generators.empty())
	{
		environment.generators["c-import"] = CImportGenerator;
		environment.generators["defun"] = DefunGenerator;
		environment.generators["var"] = VariableDeclarationGenerator;
		environment.generators["global-var"] = VariableDeclarationGenerator;
		environment.generators["static-var"] = VariableDeclarationGenerator;

		// Dispatches based on invocation name
		const char* cStatementKeywords[] = {
		    "while",
		    "return",
		    "when",
		    "array",
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

		// Note that macros always inherit the current context, whereas bodies change it
		int result = EvaluateGenerate_Recursive(environment, context, *macroOutputTokens,
		                                        /*startTokenIndex=*/0, output);
		if (result != 0)
		{
			NoteAtToken(invocationStart,
			            "Code was generated from macro. See macro expansion below:");
			printTokens(*macroOutputTokens);
			printf("\n");
			return false;
		}
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
			// The only hard-coded generator: basic function invocations. We must hard-code this
			// because we don't interpret any C/C++ in order to determine which functions are valid
			// to call (we just let the C/C++ compiler determine that for us)
			return FunctionInvocationGenerator(environment, context, tokens, invocationStartIndex,
			                                   output);
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

	const Token& token = tokens[startTokenIndex];

	if (token.type == TokenType_OpenParen)
	{
		// Invocation of a macro, generator, or function (either foreign or Cakelisp function)
		bool invocationSucceeded =
		    HandleInvocation_Recursive(environment, context, tokens, startTokenIndex, output);
		if (!invocationSucceeded)
			++numErrors;
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
					output.source.push_back({token.contents, StringOutMod_None, &token, &token});
					break;
				case TokenType_String:
					output.source.push_back(
					    {token.contents, StringOutMod_SurroundWithQuotes, &token, &token});
					break;
				default:
					ErrorAtTokenf(token,
					              "Unhandled token type %s; has a new token type been added, or "
					              "evaluator has been changed?",
					              tokenTypeToString(token.type));
					return 1;
			}
		}
		else
			numErrors++;
	}

	return numErrors;
}

// Delimiter template will be inserted between the outputs
int EvaluateGenerateAll_Recursive(EvaluatorEnvironment& environment,
                                  const EvaluatorContext& context, const std::vector<Token>& tokens,
                                  int startTokenIndex, const StringOutput& delimiterTemplate,
                                  GeneratorOutput& output)
{
	// Note that in most cases, we will continue evaluation in order to turn up more errors
	int numErrors = 0;

	int numTokens = tokens.size();
	for (int currentTokenIndex = startTokenIndex; currentTokenIndex < numTokens;
	     ++currentTokenIndex)
	{
		if (tokens[currentTokenIndex].type == TokenType_CloseParen)
		{
			// Reached the end of an argument list or body. Only modules will hit numTokens
			break;
		}

		// Starting a new argument to evaluate
		if (currentTokenIndex != startTokenIndex)
		{
			StringOutput delimiter = delimiterTemplate;
			delimiter.startToken = &tokens[currentTokenIndex];
			delimiter.endToken = &tokens[currentTokenIndex];
			// TODO: Controlling source vs. header output?
			output.source.push_back(std::move(delimiter));
		}

		numErrors +=
		    EvaluateGenerate_Recursive(environment, context, tokens, currentTokenIndex, output);

		if (tokens[currentTokenIndex].type == TokenType_OpenParen)
		{
			// Skip invocation body. for()'s increment will skip us past the final ')'
			currentTokenIndex = FindCloseParenTokenIndex(tokens, currentTokenIndex);
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
