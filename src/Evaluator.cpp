#include "Evaluator.hpp"

#include "Converters.hpp"
#include "DynamicLoader.hpp"
#include "FileUtilities.hpp"
#include "GeneratorHelpers.hpp"
#include "Generators.hpp"
#include "Logging.hpp"
#include "OutputPreambles.hpp"
#include "RunProcess.hpp"
#include "Tokenizer.hpp"
#include "Utilities.hpp"
#include "Writer.hpp"

// TODO: safe version of strcat
#include <stdio.h>
#include <string.h>

//
// Environment
//

const char* globalDefinitionName = "<global>";
const char* cakelispWorkingDir = "cakelisp_cache";

const char* g_environmentPreLinkHookSignature =
    "('manager (& ModuleManager) 'link-command (& ProcessCommand) 'link-time-inputs (* "
    "ProcessCommandInput) 'num-link-time-inputs int &return bool)";
const char* g_environmentPostReferencesResolvedHookSignature =
    "('environment (& EvaluatorEnvironment) 'was-code-modified (& bool) &return bool)";

static const char* g_environmentCompileTimeVariableDestroySignature = "('data (* void))";

GeneratorFunc findGenerator(EvaluatorEnvironment& environment, const char* functionName)
{
	GeneratorIterator findIt = environment.generators.find(std::string(functionName));
	if (findIt != environment.generators.end())
		return findIt->second;
	return nullptr;
}

static MacroFunc findMacro(EvaluatorEnvironment& environment, const char* functionName)
{
	MacroIterator findIt = environment.macros.find(std::string(functionName));
	if (findIt != environment.macros.end())
		return findIt->second;
	return nullptr;
}

void* findCompileTimeFunction(EvaluatorEnvironment& environment, const char* functionName)
{
	CompileTimeFunctionTableIterator findIt =
	    environment.compileTimeFunctions.find(std::string(functionName));
	if (findIt != environment.compileTimeFunctions.end())
		return findIt->second;
	return nullptr;
}

static bool isCompileTimeCodeLoaded(EvaluatorEnvironment& environment,
                                    const ObjectDefinition& definition)
{
	switch (definition.type)
	{
		case ObjectType_CompileTimeMacro:
			return findMacro(environment, definition.name.c_str()) != nullptr;
		case ObjectType_CompileTimeGenerator:
			return findGenerator(environment, definition.name.c_str()) != nullptr;
		case ObjectType_CompileTimeFunction:
			return findCompileTimeFunction(environment, definition.name.c_str()) != nullptr;
		default:
			return false;
	}
}

bool addObjectDefinition(EvaluatorEnvironment& environment, ObjectDefinition& definition)
{
	ObjectDefinitionMap::iterator findIt = environment.definitions.find(definition.name);
	if (findIt == environment.definitions.end())
	{
		if (isCompileTimeCodeLoaded(environment, definition))
		{
			ErrorAtTokenf(*definition.definitionInvocation,
			              "multiple definitions of %s. Name may be conflicting with built-in macro "
			              "or generator",
			              definition.name.c_str());
			return false;
		}

		environment.definitions[definition.name] = definition;
		return true;
	}
	else
	{
		ErrorAtTokenf(*definition.definitionInvocation, "multiple definitions of %s",
		              definition.name.c_str());
		NoteAtToken(*findIt->second.definitionInvocation, "first defined here");
		return false;
	}
}

ObjectDefinition* findObjectDefinition(EvaluatorEnvironment& environment, const char* name)
{
	ObjectDefinitionMap::iterator findIt = environment.definitions.find(name);
	if (findIt != environment.definitions.end())
		return &findIt->second;
	return nullptr;
}

const ObjectReferenceStatus* addObjectReference(EvaluatorEnvironment& environment,
                                                const Token& referenceNameToken,
                                                ObjectReference& reference)
{
	// Default to the module requiring the reference, for top-level references
	std::string definitionName = globalDefinitionName;
	if (!reference.context.definitionName && reference.context.scope != EvaluatorScope_Module)
		Log("error: addObjectReference() expects a definitionName\n");

	if (reference.context.definitionName)
	{
		definitionName = reference.context.definitionName->contents;
	}

	const char* defName = definitionName.c_str();
	if (log.references)
		Logf("Adding reference %s to %s\n", referenceNameToken.contents.c_str(), defName);

	// Add the reference requirement to the definition it occurred in
	ObjectReferenceStatus* refStatus = nullptr;
	ObjectDefinitionMap::iterator findDefinition = environment.definitions.find(definitionName);
	if (findDefinition == environment.definitions.end())
	{
		if (definitionName.compare(globalDefinitionName) != 0)
		{
			Logf("error: expected definition %s to already exist. Things will break\n",
			       definitionName.c_str());
		}
		else
		{
			ErrorAtTokenf(referenceNameToken,
			              "error: expected %s definition to exist as a top-level catch-all",
			              globalDefinitionName);
		}
	}
	else
	{
		// The reference is copied here somewhat unnecessarily. It would be too much of a hassle to
		// make a good link to the reference in the reference pool, because it can easily be moved
		// by hash realloc or vector resize
		ObjectReferenceStatusMap::iterator findRefIt =
		    findDefinition->second.references.find(referenceNameToken.contents.c_str());
		if (findRefIt == findDefinition->second.references.end())
		{
			ObjectReferenceStatus newStatus;
			newStatus.name = &referenceNameToken;
			newStatus.guessState = GuessState_None;
			newStatus.references.push_back(reference);
			std::pair<ObjectReferenceStatusMap::iterator, bool> newRefStatusResult =
			    findDefinition->second.references.emplace(
			        std::make_pair(referenceNameToken.contents, std::move(newStatus)));
			refStatus = &newRefStatusResult.first->second;
		}
		else
		{
			findRefIt->second.references.push_back(reference);
			refStatus = &findRefIt->second;
		}
	}

	// Add the reference to the reference pool. This makes it easier to find all places where it is
	// referenced during resolve time
	ObjectReferencePoolMap::iterator findIt =
	    environment.referencePools.find(referenceNameToken.contents);
	if (findIt == environment.referencePools.end())
	{
		ObjectReferencePool newPool = {};
		newPool.references.push_back(reference);
		environment.referencePools[referenceNameToken.contents] = std::move(newPool);
	}
	else
	{
		findIt->second.references.push_back(reference);
	}

	return refStatus;
}

int getNextFreeBuildId(EvaluatorEnvironment& environment)
{
	return ++environment.nextFreeBuildId;
}

bool isCompileTimeObject(ObjectType type)
{
	return type == ObjectType_CompileTimeMacro || type == ObjectType_CompileTimeGenerator ||
	       type == ObjectType_CompileTimeFunction;
}

bool CreateCompileTimeVariable(EvaluatorEnvironment& environment, const char* name,
                               const char* typeExpression, void* data,
                               const char* destroyCompileTimeFuncName)
{
	CompileTimeVariableTableIterator findIt = environment.compileTimeVariables.find(name);
	if (findIt != environment.compileTimeVariables.end())
	{
		Logf("error: CreateCompileTimeVariable(): variable %s already defined\n", name);
		return false;
	}

	CompileTimeVariable newCompileTimeVariable = {};
	newCompileTimeVariable.type = typeExpression;
	newCompileTimeVariable.data = data;
	if (destroyCompileTimeFuncName)
		newCompileTimeVariable.destroyCompileTimeFuncName = destroyCompileTimeFuncName;
	environment.compileTimeVariables[name] = newCompileTimeVariable;

	// Make sure it gets built and loaded once it is defined
	if (destroyCompileTimeFuncName)
		environment.requiredCompileTimeFunctions[destroyCompileTimeFuncName] =
		    "compile time variable destructor";

	return true;
}

bool GetCompileTimeVariable(EvaluatorEnvironment& environment, const char* name,
                            const char* typeExpression, void** dataOut)
{
	*dataOut = nullptr;

	CompileTimeVariableTableIterator findIt = environment.compileTimeVariables.find(name);
	if (findIt == environment.compileTimeVariables.end())
		return false;

	if (findIt->second.type.compare(typeExpression) != 0)
	{
		Logf(
		    "error: GetCompileTimeVariable(): type does not match existing variable %s. Types must "
		    "match exactly. Expected %s, got %s\n",
		    name, findIt->second.type.c_str(), typeExpression);
		return false;
	}

	*dataOut = findIt->second.data;
	return true;
}

//
// Evaluator
//

// Dispatch to a generator or expand a macro and evaluate its output recursively. If the reference
// is unknown, add it to a list so EvaluateResolveReferences() can come back and decide what to do
// with it. Only EvaluateResolveReferences() decides whether to create a C/C++ invocation
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
			ErrorAtToken(invocationName, "macro returned failure");

			// Deleting these tokens is only safe at this point because we know we have not
			// evaluated them. As soon as they are evaluated, they must be kept around
			delete macroOutputTokens;
			return false;
		}

		// The macro had no output, but we won't let that bother us
		if (macroOutputTokens->empty())
		{
			delete macroOutputTokens;
			return true;
		}

		// TODO: Pretty print to macro expand file and change output token source to
		// point there

		// Macro must generate valid parentheses pairs!
		bool validateResult = validateParentheses(*macroOutputTokens);
		if (!validateResult)
		{
			NoteAtToken(invocationStart,
			            "code was generated from macro. See erroneous macro "
			            "expansion below:");
			printTokens(*macroOutputTokens);
			Log("\n");
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
		environment.comptimeTokens.push_back(macroOutputTokens);

		// Let the definition know about the expansion so it is easy to construct an expanded list
		// of all tokens in the definition
		if (context.definitionName)
		{
			ObjectDefinitionMap::iterator findIt =
			    environment.definitions.find(context.definitionName->contents);
			if (findIt != environment.definitions.end())
			{
				ObjectDefinition& definition = findIt->second;
				definition.macroExpansions.push_back({&invocationStart, macroOutputTokens});
			}
			else
				ErrorAtTokenf(invocationStart,
				              "could not find definition '%s' to associate macro expansion "
				              "(internal code error)",
				              context.definitionName->contents.c_str());
		}

		// Note that macros always inherit the current context, whereas bodies change it
		int result = EvaluateGenerateAll_Recursive(environment, context, *macroOutputTokens,
		                                           /*startTokenIndex=*/0, output);
		if (result != 0)
		{
			NoteAtToken(invocationStart,
			            "code was generated from macro. See macro expansion below:");
			printTokens(*macroOutputTokens);
			Log("\n");
			return false;
		}

		return true;
	}

	GeneratorFunc invokedGenerator = findGenerator(environment, invocationName.contents.c_str());
	if (invokedGenerator)
	{
		environment.lastGeneratorReferences[invocationName.contents.c_str()] =
		    &tokens[invocationStartIndex];

		return invokedGenerator(environment, context, tokens, invocationStartIndex, output);
	}

	// Check for known Cakelisp functions
	ObjectDefinitionMap::iterator findIt = environment.definitions.find(invocationName.contents);
	if (findIt != environment.definitions.end() &&
	    (!isCompileTimeObject(findIt->second.type) ||
	     (findIt->second.type == ObjectType_CompileTimeFunction &&
	      findCompileTimeFunction(environment, invocationName.contents.c_str()))))
	{
		return FunctionInvocationGenerator(environment, context, tokens, invocationStartIndex,
		                                   output);
	}

	// Unknown reference
	{
		// We don't know what this is. We cannot guess it is a C/C++ function yet, because it
		// could be a generator or macro invocation that hasn't been defined yet. Leave a note
		// for the evaluator to come back to this token once a satisfying answer is found
		ObjectReference newReference = {};
		newReference.type = ObjectReferenceResolutionType_Splice;
		newReference.tokens = &tokens;
		newReference.startIndex = invocationStartIndex;
		newReference.context = context;
		// Make room for whatever gets output once this reference is resolved
		newReference.spliceOutput = new GeneratorOutput;

		// We push in a StringOutMod_Splice as a sentinel that the splice list needs to be
		// checked. Otherwise, it will be a no-op to Writer. It's useful to have this sentinel
		// so that multiple splices take up space and will then maintain sequential order
		addSpliceOutput(output, newReference.spliceOutput, &invocationStart);

		const ObjectReferenceStatus* referenceStatus =
		    addObjectReference(environment, invocationName, newReference);
		if (!referenceStatus)
		{
			ErrorAtToken(tokens[invocationStartIndex],
			             "failed to create reference status (internal error)");
			return false;
		}

		// If some action has already happened on this reference, duplicate it here
		// This code wouldn't be necessary if BuildEvaluateReferences() checked all of its reference
		// instances, and stored a status on each one. I don't like the duplication here, but it
		// does match the other HandleInvocation_Recursive() invocation types, which are handled as
		// soon as the environment has enough information to resolve the invocation
		if (referenceStatus->guessState == GuessState_Guessed ||
		    (findIt != environment.definitions.end() &&
		     findIt->second.type == ObjectType_CompileTimeFunction))
		{
			// Guess now, because BuildEvaluateReferences thinks it has already guessed all refs
			bool result =
			    FunctionInvocationGenerator(environment, newReference.context, *newReference.tokens,
			                                newReference.startIndex, *newReference.spliceOutput);
			// Our guess didn't even evaluate
			if (!result)
				return false;
		}

		// We're going to return true even though evaluation isn't yet done.
		// BuildEvaluateReferences() will handle the evaluation after it knows what the
		// references are
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
					// Special case: C requires NULL, C++ encourages nullptr. Let's handle them both
					// automatically with null
					if (token.contents.compare("null") == 0)
					{
						// TODO: C vs. C++
						addStringOutput(output.source, "nullptr", StringOutMod_None, &token);
						break;
					}

					// We need to convert what look like names in case they are lispy, but not
					// integer, character, or floating point constants
					char firstChar = token.contents[0];
					char secondChar = token.contents.size() > 1 ? token.contents[1] : 0;
					if (firstChar == '\'' || isdigit(firstChar) ||
					    (firstChar == '-' && (secondChar == '.' || isdigit(secondChar))))
						addStringOutput(output.source, token.contents, StringOutMod_None, &token);
					else
					{
						// Potential lisp name. Convert
						addStringOutput(output.source, token.contents,
						                StringOutMod_ConvertVariableName, &token);
					}
					break;
				}
				case TokenType_String:
					addStringOutput(output.source, token.contents, StringOutMod_SurroundWithQuotes,
					                &token);
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
                                  int startTokenIndex, GeneratorOutput& output)
{
	// Note that in most cases, we will continue evaluation in order to turn up more errors
	int numErrors = 0;

	bool isDelimiterUsed = !context.delimiterTemplate.output.empty() ||
	                       context.delimiterTemplate.modifiers != StringOutMod_None;
	bool isDelimiterSyntactic = !context.delimiterTemplate.output.empty() ||
	                            context.delimiterTemplate.modifiers != StringOutMod_NewlineAfter;

	// Used to detect when something was actually output during evaluation
	int lastOutputTotalSize = output.source.size() + output.header.size();

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
		if (isDelimiterUsed && currentTokenIndex != startTokenIndex)
		{
			bool outputChanged =
			    output.source.size() + output.header.size() != (unsigned long)lastOutputTotalSize;
			// If the delimiter is a newline only, it is probably for humans only, and can be
			// ignored if evaluation results in no output
			if (isDelimiterSyntactic || outputChanged)
			{
				StringOutput delimiter = context.delimiterTemplate;
				delimiter.startToken = &tokens[currentTokenIndex];
				// TODO: Controlling source vs. header output?
				output.source.push_back(std::move(delimiter));
			}
		}

		lastOutputTotalSize = output.source.size() + output.header.size();

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

bool ReplaceAndEvaluateDefinition(EvaluatorEnvironment& environment,
                                  const char* definitionToReplaceName,
                                  const std::vector<Token>& newDefinitionTokens)
{
	ObjectDefinitionMap::iterator findIt = environment.definitions.find(definitionToReplaceName);
	if (findIt == environment.definitions.end())
	{
		Logf("error: ReplaceAndEvaluateDefinition() could not find definition '%s'\n",
		       definitionToReplaceName);
		return false;
	}

	if (!validateParentheses(newDefinitionTokens))
	{
		Log("note: encountered error while validating the following replacement definition:\n");
		prettyPrintTokens(newDefinitionTokens);
		return false;
	}

	EvaluatorContext definitionContext = findIt->second.context;
	GeneratorOutput* definitionOutput = findIt->second.output;

	// This output is still referred to by the module (etc.) output's splice. When a replacement
	// definition is added, it will actually add a splice to the old definiton's output and create
	// its own output. Have the environment hold on to it for later destruction
	environment.orphanedOutputs.push_back(definitionOutput);

	// This makes me nervous because the user could have a reference to this when calling this
	// function. I can't think of a safer way to get rid of the reference without deleting it
	environment.definitions.erase(findIt);
	findIt = environment.definitions.end();

	definitionOutput->source.clear();
	definitionOutput->header.clear();

	if (definitionContext.scope == EvaluatorScope_Module)
	{
		StringOutput moduleDelimiterTemplate = {};
		moduleDelimiterTemplate.modifiers = StringOutMod_NewlineAfter;
		definitionContext.delimiterTemplate = moduleDelimiterTemplate;
	}

	bool result = EvaluateGenerateAll_Recursive(environment, definitionContext, newDefinitionTokens,
	                                            /*startTokenIndex=*/0, *definitionOutput) == 0;

	if (!result)
	{
		Log("note: encountered error while evaluating the following replacement definition:\n");
		prettyPrintTokens(newDefinitionTokens);
	}

	return result;
}

// Determine what needs to be built, iteratively
// TODO This can be made faster. I did the most naive version first, for now
static void PropagateRequiredToReferences(EvaluatorEnvironment& environment)
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

			// Automatically require a compile-time function if the environment needs it (typically
			// because some other function was called that added the requirement before the
			// definition was available)
			if (definition.type == ObjectType_CompileTimeFunction && !definition.isRequired)
			{
				RequiredCompileTimeFunctionReasonsTableIterator findIt =
				    environment.requiredCompileTimeFunctions.find(definition.name.c_str());

				if (findIt != environment.requiredCompileTimeFunctions.end())
				{
					if (log.dependencyPropagation)
						Logf("Define %s promoted to required because %s\n",
						       definition.name.c_str(), findIt->second);

					definition.isRequired = true;
					definition.environmentRequired = true;
				}
			}

			if (log.dependencyPropagation)
			{
				const char* status = definition.isRequired ? "(required)" : "(not required)";
				Logf("Define %s %s\n", definition.name.c_str(), status);
			}

			for (ObjectReferenceStatusPair& reference : definition.references)
			{
				ObjectReferenceStatus& referenceStatus = reference.second;

				if (log.dependencyPropagation)
					Logf("\tRefers to %s\n", referenceStatus.name->contents.c_str());

				if (definition.isRequired)
				{
					ObjectDefinitionMap::iterator findIt =
					    environment.definitions.find(referenceStatus.name->contents);
					if (findIt != environment.definitions.end() && !findIt->second.isRequired)
					{
						if (log.dependencyPropagation)
							Logf("\t Infecting %s with required due to %s\n",
							       referenceStatus.name->contents.c_str(), definition.name.c_str());

						++numRequiresStatusChanged;
						findIt->second.isRequired = true;
					}
				}
			}
		}
	} while (numRequiresStatusChanged);
}

static void OnCompileProcessOutput(const char* output)
{
	// TODO C/C++ error to Cakelisp token mapper
}

enum BuildStage
{
	BuildStage_None,
	BuildStage_Compiling,
	BuildStage_Linking,
	BuildStage_Loading,
	BuildStage_ResolvingReferences,
	BuildStage_Finished
};

// Note: environment.definitions can be resized/rehashed during evaluation, which invalidates
// iterators. For now, I will rely on the fact that std::unordered_map does not invalidate
// references on resize. This will need to change if the data structure changes
struct BuildObject
{
	int buildId = -1;
	int status = -1;
	BuildStage stage = BuildStage_None;
	bool hasAnyRefs = false;
	std::string artifactsName;
	std::string dynamicLibraryPath;
	std::string buildObjectName;
	ObjectDefinition* definition = nullptr;
};

int BuildExecuteCompileTimeFunctions(EvaluatorEnvironment& environment,
                                     std::vector<BuildObject>& definitionsToBuild,
                                     int& numErrorsOut)
{
	int numReferencesResolved = 0;

	// Spin up as many compile processes as necessary
	// TODO: Combine sure-thing builds into batches (ones where we know all references)
	// TODO: Instead of creating files, pipe straight to compiler?
	// TODO: Make pipeline able to start e.g. linker while other objects are still compiling
	// NOTE: definitionsToBuild must not be resized from when runProcess() is called until
	// waitForAllProcessesClosed(), else the status pointer could be invalidated
	int currentNumProcessesSpawned = 0;
	for (BuildObject& buildObject : definitionsToBuild)
	{
		ObjectDefinition* definition = buildObject.definition;

		if (log.buildProcess)
			Logf("Build %s\n", definition->name.c_str());

		if (!definition->output)
		{
			ErrorAtToken(*buildObject.definition->definitionInvocation,
			             "missing compile-time output. Internal code error?");
			continue;
		}

		char convertedNameBuffer[MAX_NAME_LENGTH] = {0};
		lispNameStyleToCNameStyle(NameStyleMode_Underscores, definition->name.c_str(),
		                          convertedNameBuffer, sizeof(convertedNameBuffer),
		                          *definition->definitionInvocation);
		char artifactsName[MAX_PATH_LENGTH] = {0};
		// Various stages will append the appropriate file extension
		PrintfBuffer(artifactsName, "comptime_%s", convertedNameBuffer);
		buildObject.artifactsName = artifactsName;
		char fileOutputName[MAX_PATH_LENGTH] = {0};
		// Writer will append the appropriate file extensions
		PrintfBuffer(fileOutputName, "%s/%s", cakelispWorkingDir,
		             buildObject.artifactsName.c_str());

		// Output definition to a file our compiler will be happy with
		// TODO: Make these come from the top
		NameStyleSettings nameSettings;
		WriterFormatSettings formatSettings;
		WriterOutputSettings outputSettings = {};

		GeneratorOutput header;
		GeneratorOutput footer;
		GeneratorOutput autoIncludes;
		makeCompileTimeHeaderFooter(header, footer, &autoIncludes,
		                            definition->definitionInvocation);
		outputSettings.heading = &header;
		outputSettings.footer = &footer;

		// Automatically include referenced compile-time function headers
		bool foundHeaders = true;
		for (ObjectReferenceStatusPair& reference : definition->references)
		{
			ObjectReferenceStatus& referenceStatus = reference.second;

			ObjectDefinitionMap::iterator findIt =
			    environment.definitions.find(referenceStatus.name->contents);
			// Ignore unknown references, because we only care about already-loaded compile-time
			// functions in this case
			if (findIt == environment.definitions.end())
				continue;

			ObjectDefinition* requiredDefinition = &findIt->second;
			// It's not really possible to invoke macros or generators because the evaluator will
			// expand them on the spot (while evaluating this definition's body)
			if (requiredDefinition->type != ObjectType_CompileTimeFunction)
				continue;

			if (requiredDefinition->compileTimeHeaderName.empty())
			{
				ErrorAtToken(*referenceStatus.name,
				             "could not find generated header for referenced compile-time "
				             "function. Internal code error?\n");
				foundHeaders = false;
				continue;
			}

			addStringOutput(autoIncludes.source, "#include", StringOutMod_SpaceAfter,
			                referenceStatus.name);
			addStringOutput(autoIncludes.source, requiredDefinition->compileTimeHeaderName.c_str(),
			                StringOutMod_SurroundWithQuotes, referenceStatus.name);
			addLangTokenOutput(autoIncludes.source, StringOutMod_NewlineAfter,
			                   referenceStatus.name);
		}
		if (!foundHeaders)
			continue;

		outputSettings.sourceCakelispFilename = fileOutputName;
		{
			char writerSourceOutputName[MAX_PATH_LENGTH] = {0};
			PrintfBuffer(writerSourceOutputName, "%s.cpp", fileOutputName);
			char writerHeaderOutputName[MAX_PATH_LENGTH] = {0};
			PrintfBuffer(writerHeaderOutputName, "%s.hpp", fileOutputName);
			outputSettings.sourceOutputName = writerSourceOutputName;
			outputSettings.headerOutputName = writerHeaderOutputName;

			// Facilitates this function being used later by other compile-time functions
			char localHeaderOutputName[MAX_PATH_LENGTH] = {0};
			PrintfBuffer(localHeaderOutputName, "%s.hpp", artifactsName);
			definition->compileTimeHeaderName = localHeaderOutputName;
		}
		// Use the separate output prepared specifically for this compile-time object
		if (!writeGeneratorOutput(*definition->output, nameSettings, formatSettings,
		                          outputSettings))
		{
			ErrorAtToken(*buildObject.definition->definitionInvocation,
			             "Failed to write to compile-time source file");
			continue;
		}

		buildObject.stage = BuildStage_Compiling;

		// The evaluator is written in C++, so all generators and macros need to support the C++
		// features used (e.g. their signatures have std::vector<>)
		char sourceOutputName[MAX_PATH_LENGTH] = {0};
		PrintfBuffer(sourceOutputName, "%s/%s.cpp", cakelispWorkingDir,
		             buildObject.artifactsName.c_str());

		char buildObjectName[MAX_PATH_LENGTH] = {0};
		PrintfBuffer(buildObjectName, "%s/%s.o", cakelispWorkingDir,
		             buildObject.artifactsName.c_str());
		buildObject.buildObjectName = buildObjectName;

		char dynamicLibraryOut[MAX_PATH_LENGTH] = {0};
		PrintfBuffer(dynamicLibraryOut, "%s/lib%s.so", cakelispWorkingDir,
		             buildObject.artifactsName.c_str());
		buildObject.dynamicLibraryPath = dynamicLibraryOut;

		if (canUseCachedFile(environment, sourceOutputName, buildObject.dynamicLibraryPath.c_str()))
		{
			if (log.buildProcess)
				Logf("Skipping compiling %s (using cached library)\n", sourceOutputName);
			// Skip straight to linking, which immediately becomes loading
			buildObject.stage = BuildStage_Linking;
			buildObject.status = 0;
			continue;
		}

		char headerInclude[MAX_PATH_LENGTH] = {0};
		if (environment.cakelispSrcDir.empty())
		{
			PrintBuffer(headerInclude, "-Isrc/");
		}
		else
		{
			PrintfBuffer(headerInclude, "-I%s", environment.cakelispSrcDir.c_str());
		}

		ProcessCommandInput compileTimeInputs[] = {
		    {ProcessCommandArgumentType_SourceInput, {sourceOutputName}},
		    {ProcessCommandArgumentType_ObjectOutput, {buildObjectName}},
		    {ProcessCommandArgumentType_CakelispHeadersInclude, {headerInclude}}};
		const char** buildArguments = MakeProcessArgumentsFromCommand(
		    environment.compileTimeBuildCommand, compileTimeInputs, ArraySize(compileTimeInputs));
		if (!buildArguments)
		{
			// TODO: Abort building if cannot invoke compiler
			continue;
		}

		RunProcessArguments compileArguments = {};
		compileArguments.fileToExecute = environment.compileTimeBuildCommand.fileToExecute.c_str();
		compileArguments.arguments = buildArguments;
		if (runProcess(compileArguments, &buildObject.status) != 0)
		{
			// TODO: Abort building if cannot invoke compiler?
			// free(buildArguments);
			// return 0;
		}

		free(buildArguments);

		// TODO: Move this to other processes as well
		// TODO This could be made smarter by allowing more spawning right when a process closes,
		// instead of starting in waves
		++currentNumProcessesSpawned;
		if (currentNumProcessesSpawned >= maxProcessesRecommendedSpawned)
		{
			waitForAllProcessesClosed(OnCompileProcessOutput);
			currentNumProcessesSpawned = 0;
		}
	}

	// The result of the builds will go straight to our definitionsToBuild
	waitForAllProcessesClosed(OnCompileProcessOutput);
	currentNumProcessesSpawned = 0;

	// Linking
	for (BuildObject& buildObject : definitionsToBuild)
	{
		if (buildObject.stage != BuildStage_Compiling)
			continue;

		if (buildObject.status != 0)
		{
			ErrorAtTokenf(*buildObject.definition->definitionInvocation,
			              "failed to compile definition '%s' with status %d",
			              buildObject.definition->name.c_str(), buildObject.status);
			// Special case: If the definition has no references, prevent it from ever having a
			// chance to fail again, because there's nothing we can do if it fails
			if (!buildObject.hasAnyRefs)
			{
				buildObject.definition->forbidBuild = true;
				NoteAtToken(*buildObject.definition->definitionInvocation,
				            "definition has no missing references. It must be a legitimate error "
				            "Cakelisp cannot correct. It will not be rebuilt");
			}

			continue;
		}

		buildObject.stage = BuildStage_Linking;

		if (log.buildProcess)
			Logf("Compiled %s successfully\n", buildObject.definition->name.c_str());

		ProcessCommandInput linkTimeInputs[] = {
		    {ProcessCommandArgumentType_DynamicLibraryOutput,
		     {buildObject.dynamicLibraryPath.c_str()}},
		    {ProcessCommandArgumentType_ObjectInput, {buildObject.buildObjectName.c_str()}}};
		const char** linkArgumentList = MakeProcessArgumentsFromCommand(
		    environment.compileTimeLinkCommand, linkTimeInputs, ArraySize(linkTimeInputs));
		if (!linkArgumentList)
		{
			// TODO: Abort building if cannot invoke compiler
			continue;
		}
		RunProcessArguments linkArguments = {};
		linkArguments.fileToExecute = environment.compileTimeLinkCommand.fileToExecute.c_str();
		linkArguments.arguments = linkArgumentList;
		if (runProcess(linkArguments, &buildObject.status) != 0)
		{
			// TODO: Abort if linker failed?
			// free(linkArgumentList);
		}
		free(linkArgumentList);
	}

	// The result of the linking will go straight to our definitionsToBuild
	waitForAllProcessesClosed(OnCompileProcessOutput);
	currentNumProcessesSpawned = 0;

	for (BuildObject& buildObject : definitionsToBuild)
	{
		if (buildObject.stage != BuildStage_Linking)
			continue;

		if (buildObject.status != 0)
		{
			ErrorAtToken(*buildObject.definition->definitionInvocation,
			             "Failed to link definition");
			continue;
		}

		buildObject.stage = BuildStage_Loading;

		if (log.buildProcess)
			Logf("Linked %s successfully\n", buildObject.definition->name.c_str());

		DynamicLibHandle builtLib = loadDynamicLibrary(buildObject.dynamicLibraryPath.c_str());
		if (!builtLib)
		{
			ErrorAtToken(*buildObject.definition->definitionInvocation,
			             "Failed to load compile-time library");
			continue;
		}

		// We need to do name conversion to be compatible with C naming
		// TODO: Make these come from the top
		NameStyleSettings nameSettings;
		char symbolNameBuffer[MAX_NAME_LENGTH] = {0};
		lispNameStyleToCNameStyle(
		    nameSettings.functionNameMode, buildObject.definition->name.c_str(), symbolNameBuffer,
		    sizeof(symbolNameBuffer), *buildObject.definition->definitionInvocation);
		void* compileTimeFunction = getSymbolFromDynamicLibrary(builtLib, symbolNameBuffer);
		if (!compileTimeFunction)
		{
			ErrorAtToken(*buildObject.definition->definitionInvocation,
			             "Failed to find symbol in loaded library");
			continue;
		}

		// Add to environment
		switch (buildObject.definition->type)
		{
			case ObjectType_CompileTimeMacro:
				if (findMacro(environment, buildObject.definition->name.c_str()))
					NoteAtToken(*buildObject.definition->definitionInvocation, "redefined macro");
				environment.macros[buildObject.definition->name] = (MacroFunc)compileTimeFunction;
				break;
			case ObjectType_CompileTimeGenerator:
				if (findGenerator(environment, buildObject.definition->name.c_str()))
					NoteAtToken(*buildObject.definition->definitionInvocation,
					            "redefined generator");
				environment.generators[buildObject.definition->name] =
				    (GeneratorFunc)compileTimeFunction;
				break;
			case ObjectType_CompileTimeFunction:
				if (findCompileTimeFunction(environment, buildObject.definition->name.c_str()))
					NoteAtToken(*buildObject.definition->definitionInvocation,
					            "redefined function");
				environment.compileTimeFunctions[buildObject.definition->name] =
				    (void*)compileTimeFunction;
				break;
			default:
				ErrorAtToken(
				    *buildObject.definition->definitionInvocation,
				    "Tried to build definition which is not compile-time object. Code error?");
				break;
		}

		buildObject.stage = BuildStage_ResolvingReferences;

		// Resolve references
		ObjectReferencePoolMap::iterator referencePoolIt =
		    environment.referencePools.find(buildObject.definition->name);
		if (referencePoolIt == environment.referencePools.end())
		{
			if (!buildObject.definition->environmentRequired)
				Log(
				    "error: built an object which had no references. It should not have been "
				    "required. There must be a problem with Cakelisp internally\n");
			continue;
		}

		bool hasErrors = false;
		std::vector<ObjectReference>& references = referencePoolIt->second.references;
		// The old-style loop must be used here because EvaluateGenerate_Recursive can add to this
		// list, which invalidates iterators
		for (int i = 0; i < (int)references.size(); ++i)
		{
			const int maxNumReferences = 1 << 13;
			if (i >= maxNumReferences)
			{
				ErrorAtTokenf(*buildObject.definition->definitionInvocation,
				              "error: definition %s exceeded max number of references (%d). Is it "
				              "in an infinite loop?",
				              buildObject.definition->name.c_str(), maxNumReferences);
				for (int n = 0; n < 10; ++n)
				{
					ErrorAtToken((*references[n].tokens)[references[n].startIndex],
					             "Reference here");
				}
				hasErrors = true;
				break;
			}

			if (references[i].isResolved)
				continue;

			if (references[i].type == ObjectReferenceResolutionType_Splice &&
			    references[i].spliceOutput)
			{
				ObjectReference* referenceValidPreEval = &references[i];
				// In case a compile-time function has already guessed the invocation was a C/C++
				// function, clear that invocation output
				resetGeneratorOutput(*referenceValidPreEval->spliceOutput);

				if (log.buildProcess)
					NoteAtToken((*referenceValidPreEval->tokens)[referenceValidPreEval->startIndex],
					            "resolving reference");

				// Evaluate from that reference
				int result = EvaluateGenerate_Recursive(
				    environment, referenceValidPreEval->context, *referenceValidPreEval->tokens,
				    referenceValidPreEval->startIndex, *referenceValidPreEval->spliceOutput);
				referenceValidPreEval = nullptr;
				hasErrors |= result > 0;
				numErrorsOut += result;
			}
			else
			{
				// Do not resolve, we don't know how to resolve this type of reference
				ErrorAtToken((*references[i].tokens)[references[i].startIndex],
				             "do not know how to resolve this reference (internal code error?)");
				hasErrors = true;
				continue;
			}

			if (hasErrors)
				continue;

			// Regardless of what evaluate turned up, we resolved this as far as we care. Trying
			// again isn't going to change the number of errors
			// Note that if new references emerge to this definition, they will automatically be
			// recognized as the definition and handled then and there, so we don't need to make
			// more than one pass
			references[i].isResolved = true;

			++numReferencesResolved;
		}

		if (hasErrors)
			continue;

		if (log.buildProcess)
			Logf("Resolved %d references\n", numReferencesResolved);

		// Remove need to build
		buildObject.definition->isLoaded = true;

		buildObject.stage = BuildStage_Finished;

		if (log.buildProcess)
			Logf("Successfully built, loaded, and executed %s\n",
			       buildObject.definition->name.c_str());
	}

	return numReferencesResolved;
}

// Returns true if progress was made resolving references (or finding new references)
bool BuildEvaluateReferences(EvaluatorEnvironment& environment, int& numErrorsOut)
{
	// We must copy references in case environment.definitions is modified, which would invalidate
	// iterators, but not references
	std::vector<ObjectDefinition*> definitionsToCheck;
	definitionsToCheck.reserve(environment.definitions.size());
	for (ObjectDefinitionPair& definitionPair : environment.definitions)
	{
		ObjectDefinition& definition = definitionPair.second;
		// Does it need to be built?
		if (!definition.isRequired)
			continue;

		// Is it already in the environment?
		if (definition.isLoaded)
			continue;

		if (definition.forbidBuild)
			continue;

		definitionsToCheck.push_back(&definition);
	}

	std::vector<BuildObject> definitionsToBuild;
	// If it's possible a definition has new requirements, make sure we do another pass to add those
	// requirements to the build
	bool requireDependencyPropagation = false;

	for (ObjectDefinition* definitionPointer : definitionsToCheck)
	{
		ObjectDefinition& definition = *definitionPointer;
		const char* defName = definition.name.c_str();

		if (log.compileTimeBuildReasons)
			Logf("Checking to build %s\n", defName);

		// Can it be built in the current environment?
		bool canBuild = true;
		bool hasRelevantChangeOccurred = false;
		bool hasGuessedRefs = false;
		bool hasAnyRefs = false;
		// If there were new guesses, we will do another pass over this definition's references in
		// case new references turned up
		bool guessMaybeDirtiedReferences = false;
		do
		{
			guessMaybeDirtiedReferences = false;

			if (definition.references.empty())
			{
				hasAnyRefs = false;
				break;
			}

			// Copy pointers to refs in case of iterator invalidation
			std::vector<ObjectReferenceStatus*> referencesToCheck;
			referencesToCheck.reserve(definition.references.size());
			for (ObjectReferenceStatusPair& referencePair : definition.references)
			{
				referencesToCheck.push_back(&referencePair.second);
			}
			for (ObjectReferenceStatus* referencePointer : referencesToCheck)
			{
				ObjectReferenceStatus& referenceStatus = *referencePointer;

				// TODO: (Performance) Add shortcut in reference
				ObjectDefinitionMap::iterator findIt =
				    environment.definitions.find(referenceStatus.name->contents);
				if (findIt != environment.definitions.end())
				{
					if (isCompileTimeObject(findIt->second.type))
					{
						bool refCompileTimeCodeLoaded = findIt->second.isLoaded;
						if (refCompileTimeCodeLoaded)
						{
							// The reference is ready to go. Built objects immediately resolve
							// references. We will react to it if the last thing we did was guess
							// incorrectly that this was a C call
							if (referenceStatus.guessState != GuessState_Resolved)
							{
								if (log.compileTimeBuildReasons)
									Log("\tRequired code has been loaded\n");

								hasRelevantChangeOccurred = true;
							}

							referenceStatus.guessState = GuessState_Resolved;
						}
						else
						{
							// If we know we are missing a compile time function, we won't try to
							// guess
							if (log.compileTimeBuildReasons)
								Logf("\tCannot build until %s is loaded\n",
								       referenceStatus.name->contents.c_str());

							referenceStatus.guessState = GuessState_WaitingForLoad;
							canBuild = false;
						}
					}
					else if (findIt->second.type == ObjectType_Function &&
					         referenceStatus.guessState != GuessState_Resolved)
					{
						// A known Cakelisp function call
						for (int i = 0; i < (int)referenceStatus.references.size(); ++i)
						{
							ObjectReference& reference = referenceStatus.references[i];
							// Run function invocation on it
							// TODO: Make invocation generator know it is a Cakelisp function
							bool result = FunctionInvocationGenerator(
							    environment, reference.context, *reference.tokens,
							    reference.startIndex, *reference.spliceOutput);
							// Our guess didn't even evaluate
							if (!result)
								canBuild = false;
						}

						referenceStatus.guessState = GuessState_Resolved;
					}
					// TODO: Building references to non-comptime functions at comptime
				}
				else
				{
					if (referenceStatus.guessState == GuessState_None)
					{
						if (log.compileTimeBuildReasons)
							Logf("\tCannot build until %s is guessed. Guessing now\n",
							       referenceStatus.name->contents.c_str());

						// Find all the times the definition makes this reference
						// We must use indices because the call to FunctionInvocationGenerator can
						// add new references to the list
						// Note that if new references are added to other functions, they need to be
						// handled in the next pass
						for (int i = 0; i < (int)referenceStatus.references.size(); ++i)
						{
							ObjectReference& reference = referenceStatus.references[i];
							// Run function invocation on it
							bool result = FunctionInvocationGenerator(
							    environment, reference.context, *reference.tokens,
							    reference.startIndex, *reference.spliceOutput);
							// Our guess didn't even evaluate
							if (!result)
								canBuild = false;
						}

						referenceStatus.guessState = GuessState_Guessed;
						hasRelevantChangeOccurred = true;
						hasGuessedRefs = true;
						guessMaybeDirtiedReferences = true;
						requireDependencyPropagation = true;
					}
					else if (referenceStatus.guessState == GuessState_Guessed)
					{
						// It has been guessed, and still isn't in definitions
						hasGuessedRefs = true;
					}
				}
			}
		} while (guessMaybeDirtiedReferences);

		// hasRelevantChangeOccurred being false suppresses rebuilding compile-time functions which
		// still have the same missing references. Note that only compile time objects can be built.
		// We put normal functions through the guessing system too because they need their functions
		// resolved as well. It's a bit dirty but not too bad
		if (canBuild && (!hasGuessedRefs || hasRelevantChangeOccurred) &&
		    isCompileTimeObject(definition.type))
		{
			BuildObject objectToBuild = {};
			objectToBuild.buildId = getNextFreeBuildId(environment);
			objectToBuild.definition = &definition;
			objectToBuild.hasAnyRefs = hasAnyRefs;
			definitionsToBuild.push_back(objectToBuild);
		}
	}

	if (log.compileTimeBuildObjects && !definitionsToBuild.empty())
	{
		int numToBuild = (int)definitionsToBuild.size();
		Logf("Building %d compile-time object%c\n", numToBuild, numToBuild > 1 ? 's' : ' ');

		for (BuildObject& buildObject : definitionsToBuild)
		{
			Logf("\t%s\n", buildObject.definition->name.c_str());
		}
	}

	int numReferencesResolved =
	    BuildExecuteCompileTimeFunctions(environment, definitionsToBuild, numErrorsOut);

	return numReferencesResolved > 0 || requireDependencyPropagation;
}

bool EvaluateResolveReferences(EvaluatorEnvironment& environment)
{
	// Print state
	if (log.references)
	{
		for (ObjectDefinitionPair& definitionPair : environment.definitions)
		{
			ObjectDefinition& definition = definitionPair.second;
			Logf("%s %s:\n", objectTypeToString(definition.type), definition.name.c_str());
			for (ObjectReferenceStatusPair& reference : definition.references)
			{
				ObjectReferenceStatus& referenceStatus = reference.second;
				Logf("\t%s\n", referenceStatus.name->contents.c_str());
			}
		}
	}

	int numBuildResolveErrors = 0;
	bool codeModified = false;
	do
	{
		// Keep propagating and evaluating until no more references are resolved. This must be done
		// in passes in case evaluation created more definitions. There's probably a smarter way,
		// but I'll do it in this brute-force style first
		bool needsAnotherPass = false;
		do
		{
			if (log.buildProcess)
				Log("Propagate references\n");

			PropagateRequiredToReferences(environment);

			if (log.buildProcess)
				Log("Build and evaluate references\n");

			needsAnotherPass = BuildEvaluateReferences(environment, numBuildResolveErrors);
			if (numBuildResolveErrors)
				break;
		} while (needsAnotherPass);

		if (numBuildResolveErrors)
			break;

		if (log.buildProcess)
			Log("Run post references resolved hooks\n");

		// At this point, all known references are resolved. Time to let the user do arbitrary code
		// generation and modification. These changes will need to be evaluated and their references
		// resolved, so we need to repeat the whole process until no more changes are made
		codeModified = false;
		for (PostReferencesResolvedHook& hook : environment.postReferencesResolvedHooks)
		{
			bool codeModifiedByHook = false;
			if (!hook(environment, codeModifiedByHook))
			{
				Log("error: hook returned failure\n");
				numBuildResolveErrors += 1;
				break;
			}

			codeModified |= codeModifiedByHook;
		}

		if (numBuildResolveErrors)
			break;
	} while (codeModified);

	// Check whether everything is resolved
	if (log.phases)
		Log("\nResult:\n");

	if (numBuildResolveErrors)
		Logf("Failed with %d errors.\n", numBuildResolveErrors);

	int errors = 0;
	for (const ObjectDefinitionPair& definitionPair : environment.definitions)
	{
		const ObjectDefinition& definition = definitionPair.second;

		if (definition.isRequired)
		{
			if (isCompileTimeObject(definition.type))
			{
				// TODO: Add ready-build runtime function check
				if (!isCompileTimeCodeLoaded(environment, definition))
				{
					// TODO: Add note for who required the object
					ErrorAtToken(*definition.definitionInvocation,
					             "Failed to build required object");
					++errors;
				}
				else
				{
					// NoteAtToken(*definition.name, "built successfully");
				}
			}
			else
			{
				// Check all references have been resolved for regular generated code
				std::vector<const Token*> missingDefinitions;
				for (const ObjectReferenceStatusPair& reference : definition.references)
				{
					const ObjectReferenceStatus& referenceStatus = reference.second;

					// TODO: (Performance) Add shortcut in reference
					ObjectDefinitionMap::iterator findIt =
					    environment.definitions.find(referenceStatus.name->contents);
					if (findIt != environment.definitions.end())
					{
						if (isCompileTimeObject(findIt->second.type) &&
						    !isCompileTimeCodeLoaded(environment, findIt->second))
						{
							missingDefinitions.push_back(findIt->second.definitionInvocation);
							++errors;
						}
					}

					if (referenceStatus.guessState == GuessState_None)
					{
						ErrorAtToken(*referenceStatus.name, "reference has not been resolved");
					}
				}

				if (!missingDefinitions.empty())
				{
					ErrorAtTokenf(*definition.definitionInvocation, "failed to generate %s",
					              definition.name.c_str());
					for (const Token* missingDefinition : missingDefinitions)
						NoteAtToken(*missingDefinition,
						            "missing compile-time function defined here");
				}
			}
		}
		else
		{
			if (log.buildOmissions && isCompileTimeObject(definition.type))
				NoteAtTokenf(*definition.definitionInvocation,
				             "did not build %s (not required by any module)",
				             definition.name.c_str());
		}
	}

	return errors == 0 && numBuildResolveErrors == 0;
}

// This serves only as a warning. I want to be very explicit with the lifetime of tokens
EvaluatorEnvironment::~EvaluatorEnvironment()
{
	if (!comptimeTokens.empty())
	{
		Log(
		    "Warning: environmentDestroyInvalidateTokens() has not been called. This will leak "
		    "memory.\n Call it once you are certain no tokens in any expansions will be "
		    "referenced.\n");
	}
}

void environmentDestroyInvalidateTokens(EvaluatorEnvironment& environment)
{
	for (CompileTimeVariableTablePair& compileTimeVariablePair : environment.compileTimeVariables)
	{
		const std::string& destroyFuncName =
		    compileTimeVariablePair.second.destroyCompileTimeFuncName;
		if (!destroyFuncName.empty())
		{
			// Search for the compile-time function. It needs to have been required in order to be
			// built and loaded already
			void* destroyFunc = findCompileTimeFunction(environment, destroyFuncName.c_str());
			if (destroyFunc)
			{
				static std::vector<Token> expectedSignature;
				if (expectedSignature.empty())
				{
					if (!tokenizeLinePrintError(g_environmentCompileTimeVariableDestroySignature,
					                            __FILE__, __LINE__, expectedSignature))
					{
						Log(
						    "error: failed to tokenize "
						    "g_environmentCompileTimeVariableDestroySignature! Internal code "
						    "error. Compile time variable memory will leak\n");
						continue;
					}
				}

				ObjectDefinition* destroyFuncDefinition =
				    findObjectDefinition(environment, destroyFuncName.c_str());
				if (!destroyFuncDefinition)
				{
					Log(
					    "error: could not find compile-time variable destroy function to verify "
					    "signature. Internal code error?\n");
					continue;
				}

				// If it got this far, it's a safe bet that the signature is valid. Skip the open
				// paren, defun-comptime, and name
				const Token* startSignatureToken = destroyFuncDefinition->definitionInvocation + 3;

				if (!CompileTimeFunctionSignatureMatches(environment, *startSignatureToken,
				                                         destroyFuncName.c_str(),
				                                         expectedSignature))
				{
					continue;
				}

				((CompileTimeVariableDestroyFunc)destroyFunc)(compileTimeVariablePair.second.data);
			}
			else
			{
				Logf(
				    "error: destruction function '%s' for compile-time variable '%s' was not "
				    "loaded before the environment started destruction. Was it ever defined, or "
				    "defined but not required? Memory will leak\n",
				    destroyFuncName.c_str(), compileTimeVariablePair.first.c_str());
			}
		}
		else
			free(compileTimeVariablePair.second.data);
	}
	environment.compileTimeVariables.clear();

	for (ObjectReferencePoolPair& referencePoolPair : environment.referencePools)
	{
		for (ObjectReference& reference : referencePoolPair.second.references)
		{
			delete reference.spliceOutput;
		}
		referencePoolPair.second.references.clear();
	}
	environment.referencePools.clear();

	for (ObjectDefinitionPair& definitionPair : environment.definitions)
	{
		ObjectDefinition& definition = definitionPair.second;
		delete definition.output;
	}
	environment.definitions.clear();

	for (GeneratorOutput* output : environment.orphanedOutputs)
	{
		delete output;
	}
	environment.orphanedOutputs.clear();

	for (const std::vector<Token>* comptimeTokens : environment.comptimeTokens)
		delete comptimeTokens;
	environment.comptimeTokens.clear();
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
		case ObjectType_PseudoObject:
			return "Pseudo-object";
		case ObjectType_Function:
			return "Function";
		case ObjectType_Variable:
			return "Variable";
		case ObjectType_CompileTimeMacro:
			return "Macro";
		case ObjectType_CompileTimeGenerator:
			return "Generator";
		case ObjectType_CompileTimeFunction:
			return "Compile-time Function";
		default:
			return "Unknown";
	}
}

void resetGeneratorOutput(GeneratorOutput& output)
{
	output.source.clear();
	output.header.clear();
	output.functions.clear();
	output.imports.clear();
}

bool canUseCachedFile(EvaluatorEnvironment& environment, const char* filename,
                      const char* reference)
{
	if (environment.useCachedFiles)
		return !fileIsMoreRecentlyModified(filename, reference);
	else
		return false;
}

bool searchForFileInPaths(const char* shortPath, const char* encounteredInFile,
                          const std::vector<std::string>& searchPaths, char* foundFilePathOut,
                          int foundFilePathOutSize)
{
	// First, check if it's relative to the file it was encountered in
	if (encounteredInFile)
	{
		char relativePathBuffer[MAX_PATH_LENGTH] = {0};
		getDirectoryFromPath(encounteredInFile, relativePathBuffer, sizeof(relativePathBuffer));
		SafeSnprinf(foundFilePathOut, foundFilePathOutSize, "%s/%s", relativePathBuffer, shortPath);

		if (log.fileSearch)
			Logf("File exists? %s (", foundFilePathOut);

		if (fileExists(foundFilePathOut))
		{
			if (log.fileSearch)
				Log("yes)\n");
			return true;
		}
		if (log.fileSearch)
			Log("no)\n");
	}

	for (const std::string& path : searchPaths)
	{
		SafeSnprinf(foundFilePathOut, foundFilePathOutSize, "%s/%s", path.c_str(), shortPath);

		if (log.fileSearch)
			Logf("File exists? %s (", foundFilePathOut);

		if (fileExists(foundFilePathOut))
		{
			if (log.fileSearch)
				Log("yes)\n");
			return true;
		}
		if (log.fileSearch)
			Log("no)\n");
	}

	if (log.fileSearch)
		Logf("> Not found: %s\n", shortPath);

	return false;
}

bool searchForFileInPathsWithError(const char* shortPath, const char* encounteredInFile,
                                   const std::vector<std::string>& searchPaths,
                                   char* foundFilePathOut, int foundFilePathOutSize,
                                   const Token& blameToken)
{
	if (!searchForFileInPaths(shortPath, encounteredInFile, searchPaths, foundFilePathOut,
	                          foundFilePathOutSize))
	{
		ErrorAtToken(blameToken, "file not found! Checked the following paths:");
		Logf("Checked if relative to %s\n", encounteredInFile);
		Log("Checked search paths:\n");
		for (const std::string& path : searchPaths)
		{
			Logf("\t%s\n", path.c_str());
		}
		return false;
	}

	return true;
}
