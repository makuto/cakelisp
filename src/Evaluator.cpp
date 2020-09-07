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
	GeneratorIterator findIt = environment.generators.find(std::string(functionName));
	if (findIt != environment.generators.end())
		return findIt->second;
	return nullptr;
}

MacroFunc findMacro(EvaluatorEnvironment& environment, const char* functionName)
{
	MacroIterator findIt = environment.macros.find(std::string(functionName));
	if (findIt != environment.macros.end())
		return findIt->second;
	return nullptr;
}

bool addObjectDefinition(EvaluatorEnvironment& environment, ObjectDefinition& definition)
{
	ObjectDefinitionMap::iterator findIt = environment.definitions.find(definition.name->contents);
	if (findIt == environment.definitions.end())
	{
		environment.definitions[definition.name->contents] = definition;
		return true;
	}
	else
	{
		ErrorAtTokenf(*definition.name, "multiple definitions of %s",
		              definition.name->contents.c_str());
		NoteAtToken(*findIt->second.name, "first defined here");
		return false;
	}
}

void addObjectReference(EvaluatorEnvironment& environment, EvaluatorContext context,
                        ObjectReference& reference)
{
	// Default to the module requiring the reference, for top-level references
	const char* moduleDefinitionName = "<module>";
	std::string definitionName = moduleDefinitionName;
	if (context.definitionName)
	{
		definitionName = context.definitionName->contents;
	}

	// TODO This will become necessary once references store referents (append not overwrite)
	// ObjectReferenceMap::iterator findIt = environment.references.find(definitionName);
	// if (findIt == environment.references.end())
	// {
	environment.references[definitionName] = reference;
	// }
	// else
	// {
	// }

	// Add the reference requirement to the definition we're working on
	ObjectDefinitionMap::iterator findDefinition = environment.definitions.find(definitionName);
	if (findDefinition == environment.definitions.end())
	{
		if (definitionName.compare(moduleDefinitionName) != 0)
		{
			printf("error: expected definition %s to already exist. Things will break\n",
			       definitionName.c_str());
		}
		else
		{
			// TODO Add the <module> definition lazily?
			// environment.definitions[moduleDefinitionName] = newDefinition;// {reference.name};
		}
	}
	else
	{
		findDefinition->second.references[reference.name->contents] = {reference.name};
	}
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
		else
		{
			// Reference resolver v2
			ObjectReference newReference;
			newReference.tokens = &tokens;
			newReference.startIndex = invocationStartIndex;
			newReference.name = &invocationName;
			newReference.isRequired = context.isRequired;
			addObjectReference(environment, context, newReference);

			// We don't know what this is. We cannot guess it is a C/C++ function yet, because it
			// could be a generator or macro invocation that hasn't been defined yet. Leave a note
			// for the evaluator to come back to this token once a satisfying answer is found
			UnknownReference unknownInvocation = {};
			unknownInvocation.tokens = &tokens;
			unknownInvocation.startIndex = invocationStartIndex;
			unknownInvocation.symbolReference = &invocationName;
			unknownInvocation.context = context;
			unknownInvocation.type = UnknownReferenceType_Invocation;

			// Prepare our output splice, which is where output should go once the symbol is
			// resolved. Rather than actually inserting and causing potentially massive shifts in
			// the output list, the splice will redirect to an external output list. It is the
			// responsibility of the Writer to watch for splices
			unknownInvocation.output = &output.source;
			// We push in a StringOutMod_Splice as a sentinel that the splice list needs to be
			// checked. Otherwise, it will be a no-op to Writer. It's useful to have this sentinel
			// so that multiple splices take up space and will then maintain sequential order
			output.source.push_back(
			    {EmptyString, StringOutMod_Splice, &invocationStart, &invocationStart});
			unknownInvocation.spliceOutputIndex = output.source.size() - 1;

			if (context.isMacroOrGeneratorDefinition)
				environment.unknownReferencesForCompileTime.push_back(unknownInvocation);
			else
				environment.unknownReferences.push_back(unknownInvocation);

			// We're going to return true even though evaluation isn't yet done. The topmost
			// evaluate call should handle resolving all unknown references
		}

		// TODO Relocate to symbol resolver
		// else if (context.scope == EvaluatorScope_Module)
		// {
		// 	ErrorAtTokenf(invocationStart,
		// 	              "Unknown function %s. Only macros and generators may be "
		// 	              "invoked at top level",
		// 	              invocationName.contents.c_str());
		// 	return false;
		// }
		// else
		// {
		// 	// The only hard-coded generator: basic function invocations. We must hard-code this
		// 	// because we don't interpret any C/C++ in order to determine which functions are valid
		// 	// to call (we just let the C/C++ compiler determine that for us)
		// 	return FunctionInvocationGenerator(environment, context, tokens, invocationStartIndex,
		// 	                                   output);
		// }
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
				{
					// We need to convert what look like names in case they are lispy, but not
					// integer, character, or floating point constants
					char firstChar = token.contents[0];
					char secondChar = token.contents.size() > 1 ? token.contents[1] : 0;
					if (firstChar == '\'' || isdigit(firstChar) ||
					    (firstChar == '-' && (secondChar == '.' || isdigit(secondChar))))
						output.source.push_back(
						    {token.contents, StringOutMod_None, &token, &token});
					else
					{
						// Potential lisp name. Convert
						output.source.push_back(
						    {token.contents, StringOutMod_ConvertVariableName, &token, &token});
					}
					break;
				}
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

bool EvaluateResolveReferencesV1(EvaluatorEnvironment& environment)
{
	// Stop as soon as we do a loop which made no progress
	int numReferencesResolved = 0;
	do
	{
		for (const UnknownReference& reference : environment.unknownReferencesForCompileTime)
		{
			const std::string& referenceName = reference.symbolReference->contents;
			MacroFunc macro = findMacro(environment, referenceName.c_str());
			if (macro)
			{
				++numReferencesResolved;
			}
			GeneratorFunc generator = findGenerator(environment, referenceName.c_str());
			if (generator)
			{
				++numReferencesResolved;
				// TODO Evaluate
			}
		}
	} while (numReferencesResolved);

	// No more macros/generators are resolvable. Guess that the remaining references are C/C++
	// function calls
	// TODO: backtrack if it turns out they aren't? e.g. in the case of a macro which writes another
	// macro, which a generator uses
	// for (const UnknownReference& reference : environment.unknownReferencesForCompileTime)
	// {
	// 	// The only hard-coded generator: basic function invocations. We must hard-code this
	// 	// because we don't interpret any C/C++ in order to determine which functions are valid
	// 	// to call (we just let the C/C++ compiler determine that for us)
	// 	// if (FunctionInvocationGenerator(environment, reference.context, *reference.tokens,
	// 	                                // reference.startIndex, output))
	// 	{
	// 	}
	// }

	return false;
}

const char* objectTypeToString(ObjectType type);

// TODO This can be made faster. I did the most naive version first, for now
void PropagateRequiredToReferences(EvaluatorEnvironment& environment)
{
	// Figure out what is required
	// This needs to loop so long as it doesn't recurse to references
	int numRequiresStatusChanged = 0;
	do
	{
		numRequiresStatusChanged = 0;
		for (ObjectDefinitionPair& definitionPair : environment.definitions)
		{
			ObjectDefinition& definition = definitionPair.second;
			const char* status = definition.isRequired ? "(required)" : "(not required)";
			printf("Define %s %s\n", definition.name->contents.c_str(), status);

			for (ObjectReferenceStatusPair& reference : definition.references)
			{
				ObjectReferenceStatus& referenceStatus = reference.second;
				printf("\tRefers to %s\n", referenceStatus.name->contents.c_str());
				if (definition.isRequired)
				{
					ObjectDefinitionMap::iterator findIt =
					    environment.definitions.find(referenceStatus.name->contents);
					if (findIt != environment.definitions.end() &&
					    !findIt->second.isRequired)
					{
						printf("\t Infecting %s with required due to %s\n",
						       referenceStatus.name->contents.c_str(),
						       definition.name->contents.c_str());
						++numRequiresStatusChanged;
						findIt->second.isRequired = true;
					}
					// TODO Recurse, infecting all of the definition's references as required?
				}
			}
		}
	} while (numRequiresStatusChanged);
}

bool EvaluateResolveReferences(EvaluatorEnvironment& environment)
{
	// Print state
	for (ObjectDefinitionPair& definitionPair : environment.definitions)
	{
		ObjectDefinition& definition = definitionPair.second;
		printf("%s %s:\n", objectTypeToString(definition.type), definition.name->contents.c_str());
		for (ObjectReferenceStatusPair& reference : definition.references)
		{
			ObjectReferenceStatus& referenceStatus = reference.second;
			printf("\t%s\n", referenceStatus.name->contents.c_str());
		}
	}

	PropagateRequiredToReferences(environment);

	// Check everything is resolved
	int errors = 0;
	for (const ObjectDefinitionPair& definitionPair : environment.definitions)
	{
		const ObjectDefinition& definition = definitionPair.second;
		printf("%s:\n", definition.name->contents.c_str());
		if (definition.isRequired)
		{
			// TODO: Add ready-build runtime function check
			if (!findMacro(environment, definition.name->contents.c_str()) &&
			    !findGenerator(environment, definition.name->contents.c_str()))
			{
				// TODO: Add note for who required the object
				ErrorAtToken(*definition.name, "\tFailed to build required object\n");
				++errors;
			}
			else
				printf("\tBuilt successfully\n");
		}
		else
		{
			NoteAtToken(*definition.name, "omitted (not required by module)\n");
		}
	}

	return errors == 0;
}

// This serves only as a warning. I want to be very explicit with the lifetime of tokens
EvaluatorEnvironment::~EvaluatorEnvironment()
{
	if (!macroExpansions.empty())
	{
		printf(
		    "Warning: environmentDestroyInvalidateTokens() has not been called. This will leak "
		    "memory.\n Call it once you are certain no tokens in any expansions will be "
		    "referenced.\n");
	}
}

void environmentDestroyInvalidateTokens(EvaluatorEnvironment& environment)
{
	for (CompileTimeFunctionDefiniton& function : environment.compileTimeFunctions)
	{
		delete function.output;
	}
	environment.compileTimeFunctions.clear();

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

const char* objectTypeToString(ObjectType type)
{
	switch (type)
	{
		case ObjectType_Function:
			return "Function";
		case ObjectType_CompileTimeFunction:
			return "CompileTimeFunction";
		default:
			return "Unknown";
	}
}
