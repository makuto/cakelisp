#include "Generators.hpp"

#include <string.h>

#include <algorithm>

#include "Converters.hpp"
#include "Evaluator.hpp"
#include "FileUtilities.hpp"
#include "GeneratorHelpers.hpp"
#include "GeneratorHelpersEnums.hpp"
#include "Logging.hpp"
#include "ModuleManager.hpp"
#include "Tokenizer.hpp"
#include "Utilities.hpp"

// (export
const int EXPORT_SCOPE_START_EVAL_OFFSET = 2;

typedef bool (*ProcessCommandOptionFunc)(EvaluatorEnvironment& environment,
                                         const std::vector<Token>& tokens, int startTokenIndex,
                                         ProcessCommand* command);

bool SetProcessCommandFileToExec(EvaluatorEnvironment& environment,
                                 const std::vector<Token>& tokens, int startTokenIndex,
                                 ProcessCommand* command)
{
	int endInvocationIndex = FindCloseParenTokenIndex(tokens, startTokenIndex);
	int argumentIndex = getExpectedArgument("expected path to compiler", tokens, startTokenIndex, 2,
	                                        endInvocationIndex);
	if (argumentIndex == -1)
		return false;

	const Token& argumentToken = tokens[argumentIndex];

	if (!ExpectTokenType("file to execute", argumentToken, TokenType_String))
		return false;

	command->fileToExecute = argumentToken.contents;
	return true;
}

bool SetProcessCommandArguments(EvaluatorEnvironment& environment, const std::vector<Token>& tokens,
                                int startTokenIndex, ProcessCommand* command)
{
	command->arguments.clear();

	int endInvocationIndex = FindCloseParenTokenIndex(tokens, startTokenIndex);
	int startArgsIndex = getArgument(tokens, startTokenIndex, 2, endInvocationIndex);
	// No args is weird, but we'll allow it
	if (startArgsIndex == -1)
		return true;

	for (int argumentIndex = startArgsIndex; argumentIndex < endInvocationIndex; ++argumentIndex)
	{
		const Token& argumentToken = tokens[argumentIndex];
		if (argumentToken.type == TokenType_String)
		{
			command->arguments.push_back(
			    {ProcessCommandArgumentType_String, argumentToken.contents});
		}
		else if (argumentToken.type == TokenType_Symbol)
		{
			struct
			{
				const char* symbolName;
				ProcessCommandArgumentType type;
			} symbolsToCommandTypes[] = {
			    {"'source-input", ProcessCommandArgumentType_SourceInput},
			    {"'object-output", ProcessCommandArgumentType_ObjectOutput},
			    {"'debug-symbols-output", ProcessCommandArgumentType_DebugSymbolsOutput},
			    {"'import-library-paths", ProcessCommandArgumentType_ImportLibraryPaths},
			    {"'import-libraries", ProcessCommandArgumentType_ImportLibraries},
			    {"'cakelisp-headers-include", ProcessCommandArgumentType_CakelispHeadersInclude},
			    {"'include-search-dirs", ProcessCommandArgumentType_IncludeSearchDirs},
			    {"'additional-options", ProcessCommandArgumentType_AdditionalOptions},
			    {"'precompiled-header-output", ProcessCommandArgumentType_PrecompiledHeaderOutput},
			    {"'precompiled-header-include",
			     ProcessCommandArgumentType_PrecompiledHeaderInclude},
			    {"'object-input", ProcessCommandArgumentType_ObjectInput},
			    {"'library-output", ProcessCommandArgumentType_DynamicLibraryOutput},
			    {"'executable-output", ProcessCommandArgumentType_ExecutableOutput},
			    {"'library-search-dirs", ProcessCommandArgumentType_LibrarySearchDirs},
			    {"'libraries", ProcessCommandArgumentType_Libraries},
			    {"'library-runtime-search-dirs",
			     ProcessCommandArgumentType_LibraryRuntimeSearchDirs},
			    {"'linker-arguments", ProcessCommandArgumentType_LinkerArguments},
			};
			bool found = false;
			for (unsigned int i = 0; i < ArraySize(symbolsToCommandTypes); ++i)
			{
				if (argumentToken.contents.compare(symbolsToCommandTypes[i].symbolName) == 0)
				{
					command->arguments.push_back({symbolsToCommandTypes[i].type, EmptyString});
					found = true;
					break;
				}
			}
			if (!found)
			{
				ErrorAtToken(argumentToken,
				             "unrecognized argument symbol. Recognized options (some may not be "
				             "suitable for this command):");
				for (unsigned int i = 0; i < ArraySize(symbolsToCommandTypes); ++i)
				{
					Logf("\t%s\n", symbolsToCommandTypes[i].symbolName);
				}
				return false;
			}
		}
		else
		{
			ErrorAtTokenf(argumentToken, "expected string argument or symbol, got %s",
			              tokenTypeToString(argumentToken.type));
			return false;
		}
	}
	return true;
}

bool SetCakelispOption(EvaluatorEnvironment& environment, const EvaluatorContext& context,
                       const std::vector<Token>& tokens, int startTokenIndex,
                       GeneratorOutput& output)
{
	// Don't let the user think this function can be called during comptime
	if (!ExpectEvaluatorScope("set-cakelisp-option", tokens[startTokenIndex], context,
	                          EvaluatorScope_Module))
		return false;

	int endInvocationIndex = FindCloseParenTokenIndex(tokens, startTokenIndex);

	int optionNameIndex =
	    getExpectedArgument("expected option name", tokens, startTokenIndex, 1, endInvocationIndex);
	if (optionNameIndex == -1)
		return false;

	struct
	{
		const char* option;
		std::string* output;
	} stringOptions[] = {
		{"cakelisp-src-dir", &environment.cakelispSrcDir},
		{"cakelisp-lib-dir", &environment.cakelispLibDir},
	    {"executable-output", &environment.executableOutput},
	};
	for (unsigned int i = 0; i < ArraySize(stringOptions); ++i)
	{
		if (tokens[optionNameIndex].contents.compare(stringOptions[i].option) == 0)
		{
			int pathIndex = getExpectedArgument("expected value", tokens, startTokenIndex, 2,
			                                    endInvocationIndex);
			if (pathIndex == -1)
				return false;

			const Token& pathToken = tokens[pathIndex];

			// This is a bit unfortunate. Because I don't have an interpreter, this must be a type
			// we can recognize, and cannot be constructed procedurally
			if (!ExpectTokenType(stringOptions[i].option, pathToken, TokenType_String))
				return false;

			if (!stringOptions[i].output->empty())
			{
				if (logging.optionAdding)
					NoteAtTokenf(
					    pathToken,
					    "ignoring %s - only the first encountered set will have an effect. "
					    "Currently set to '%s'",
					    stringOptions[i].option, stringOptions[i].output->c_str());
				return true;
			}

			*stringOptions[i].output = pathToken.contents;
			return true;
		}
	}

	// This needs to be defined early, else things will only be partially supported
	if (tokens[optionNameIndex].contents.compare("use-c-linkage") == 0)
	{
		int enableStateIndex =
		    getExpectedArgument("expected true or false", tokens, startTokenIndex, 2, endInvocationIndex);
		if (enableStateIndex == -1)
			return false;

		const Token& enableStateToken = tokens[enableStateIndex];

		if (!ExpectTokenType("use-c-linkage", enableStateToken, TokenType_Symbol))
			return false;

		if (enableStateToken.contents.compare("true") == 0)
			environment.useCLinkage = true;
		else if (enableStateToken.contents.compare("false") == 0)
			environment.useCLinkage = false;
		else
		{
			ErrorAtToken(enableStateToken, "expected true or false");
			return false;
		}

		return true;
	}

	struct ProcessCommandOptions
	{
		const char* optionName;
		ProcessCommand* command;
		ProcessCommandOptionFunc handler;
	};
	ProcessCommandOptions commandOptions[] = {
	    {"compile-time-compiler", &environment.compileTimeBuildCommand,
	     SetProcessCommandFileToExec},
	    {"compile-time-compile-arguments", &environment.compileTimeBuildCommand,
	     SetProcessCommandArguments},
	    {"compile-time-linker", &environment.compileTimeLinkCommand, SetProcessCommandFileToExec},
	    {"compile-time-link-arguments", &environment.compileTimeLinkCommand,
	     SetProcessCommandArguments},
	    {"compile-time-header-precompiler", &environment.compileTimeHeaderPrecompilerCommand,
	     SetProcessCommandFileToExec},
	    {"compile-time-header-precompiler-arguments",
	     &environment.compileTimeHeaderPrecompilerCommand, SetProcessCommandArguments},
	    {"build-time-compiler", &environment.buildTimeBuildCommand, SetProcessCommandFileToExec},
	    {"build-time-compile-arguments", &environment.buildTimeBuildCommand,
	     SetProcessCommandArguments},
	    {"build-time-linker", &environment.buildTimeLinkCommand, SetProcessCommandFileToExec},
	    {"build-time-link-arguments", &environment.buildTimeLinkCommand,
	     SetProcessCommandArguments},
	};

	for (unsigned int i = 0; i < ArraySize(commandOptions); ++i)
	{
		if (tokens[optionNameIndex].contents.compare(commandOptions[i].optionName) == 0)
		{
			return commandOptions[i].handler(environment, tokens, startTokenIndex,
			                                 commandOptions[i].command);
		}
	}

	ErrorAtToken(tokens[optionNameIndex], "unrecognized option. Available options:");
	for (unsigned int i = 0; i < ArraySize(stringOptions); ++i)
		Logf("\t%s\n", stringOptions[i].option);
	for (unsigned int i = 0; i < ArraySize(commandOptions); ++i)
		Logf("\t%s\n", commandOptions[i].optionName);
	return false;
}

bool SetModuleOption(EvaluatorEnvironment& environment, const EvaluatorContext& context,
                     const std::vector<Token>& tokens, int startTokenIndex, GeneratorOutput& output)
{
	// Don't let the user think this function can be called during comptime
	if (!ExpectEvaluatorScope("set-module-option", tokens[startTokenIndex], context,
	                          EvaluatorScope_Module))
		return false;

	if (!context.module)
	{
		ErrorAtToken(tokens[startTokenIndex], "modules not supported (internal code error?)");
		return false;
	}

	int endInvocationIndex = FindCloseParenTokenIndex(tokens, startTokenIndex);

	int optionNameIndex =
	    getExpectedArgument("expected option name", tokens, startTokenIndex, 1, endInvocationIndex);
	if (optionNameIndex == -1)
		return false;

	// TODO: Copy-pasted
	struct ProcessCommandOptions
	{
		const char* optionName;
		ProcessCommand* command;
		ProcessCommandOptionFunc handler;
	};
	ProcessCommandOptions commandOptions[] = {
	    // TODO: Use module overrides
	    // {"compile-time-compiler", &context.module->compileTimeBuildCommand,
	    //  SetProcessCommandFileToExec},
	    // {"compile-time-compile-arguments", &context.module->compileTimeBuildCommand,
	    //  SetProcessCommandArguments},
	    // {"compile-time-linker", &context.module->compileTimeLinkCommand,
	    //  SetProcessCommandFileToExec},
	    // {"compile-time-link-arguments", &context.module->compileTimeLinkCommand,
	    //  SetProcessCommandArguments},
	    {"build-time-compiler", &context.module->buildTimeBuildCommand,
	     SetProcessCommandFileToExec},
	    {"build-time-compile-arguments", &context.module->buildTimeBuildCommand,
	     SetProcessCommandArguments},
	    // Doesn't really make sense
	    // {"build-time-linker", &context.module->buildTimeLinkCommand,
	    // SetProcessCommandFileToExec},
	    // {"build-time-link-arguments", &context.module->buildTimeLinkCommand,
	    // SetProcessCommandArguments},
	};

	for (unsigned int i = 0; i < ArraySize(commandOptions); ++i)
	{
		if (tokens[optionNameIndex].contents.compare(commandOptions[i].optionName) == 0)
		{
			return commandOptions[i].handler(environment, tokens, startTokenIndex,
			                                 commandOptions[i].command);
		}
	}

	ErrorAtToken(tokens[optionNameIndex], "unrecognized option");
	return false;
}

bool AddCompileTimeHookGenerator(EvaluatorEnvironment& environment, const EvaluatorContext& context,
                                 const std::vector<Token>& tokens, int startTokenIndex,
                                 GeneratorOutput& output)
{
	// Don't let the user think this function can be called during comptime
	if (!ExpectEvaluatorScope("add-compile-time-hook", tokens[startTokenIndex], context,
	                          EvaluatorScope_Module))
		return false;

	// Without "-module", the hook is executed at environment-level
	bool isModuleHook =
	    tokens[startTokenIndex + 1].contents.compare("add-compile-time-hook-module") == 0;

	int endInvocationIndex = FindCloseParenTokenIndex(tokens, startTokenIndex);

	int hookNameIndex =
	    getExpectedArgument("expected hook name", tokens, startTokenIndex, 1, endInvocationIndex);
	if (hookNameIndex == -1 ||
	    !ExpectTokenType("compile-time hook", tokens[hookNameIndex], TokenType_Symbol))
		return false;

	int functionNameIndex = getExpectedArgument("expected function name", tokens, startTokenIndex,
	                                            2, endInvocationIndex);
	if (functionNameIndex == -1 ||
	    !ExpectTokenType("compile-time hook", tokens[functionNameIndex], TokenType_Symbol))
		return false;

	int userPriority = 0;
	int priorityIndex = getArgument(tokens, startTokenIndex, 3, endInvocationIndex);
	if (priorityIndex != -1)
	{
		bool isPriorityIncrease = tokens[priorityIndex].contents.compare(":priority-increase") == 0;
		bool isPriorityDecrease = !isPriorityIncrease &&
		                          tokens[priorityIndex].contents.compare(":priority-decrease") == 0;
		if (!isPriorityIncrease && !isPriorityDecrease)
		{
			ErrorAtToken(tokens[priorityIndex],
			             "expected optional :priority-decrease or :priority-increase keyword, got "
			             "unknown symbol");
			return false;
		}

		int priorityValueIndex = getExpectedArgument("expected integer priority", tokens,
		                                             startTokenIndex, 4, endInvocationIndex);
		if (priorityValueIndex == -1 ||
		    !ExpectTokenType("compile-time hook priority", tokens[priorityValueIndex],
		                     TokenType_Symbol))
			return false;

		userPriority = atoi(tokens[priorityValueIndex].contents.c_str());
		if (userPriority < 0)
		{
			ErrorAtTokenf(tokens[priorityValueIndex],
			              "only positive integers are allowed. If you want to decrease priority, "
			              "use :priority-decrease %d instead",
			              userPriority);
			return false;
		}

		if (isPriorityDecrease)
			userPriority = -userPriority;
	}

	void* hookFunction =
	    findCompileTimeFunction(environment, tokens[functionNameIndex].contents.c_str());
	if (hookFunction)
	{
		const Token& hookName = tokens[hookNameIndex];
		if (isModuleHook && hookName.contents.compare("pre-build") == 0)
		{
			if (!context.module)
			{
				ErrorAtToken(
				    tokens[startTokenIndex],
				    "context doesn't provide module to override hook. Internal code error?");
				return false;
			}

			return AddCompileTimeHook(environment, &context.module->preBuildHooks,
			                          g_modulePreBuildHookSignature,
			                          tokens[functionNameIndex].contents.c_str(), hookFunction,
			                          userPriority, &tokens[functionNameIndex]);
		}

		if (!isModuleHook && hookName.contents.compare("pre-link") == 0)
		{
			return AddCompileTimeHook(environment, &environment.preLinkHooks,
			                          g_environmentPreLinkHookSignature,
			                          tokens[functionNameIndex].contents.c_str(), hookFunction,
			                          userPriority, &tokens[functionNameIndex]);
		}

		if (!isModuleHook && hookName.contents.compare("post-references-resolved") == 0)
		{
			return AddCompileTimeHook(environment, &environment.postReferencesResolvedHooks,
			                          g_environmentPostReferencesResolvedHookSignature,
			                          tokens[functionNameIndex].contents.c_str(), hookFunction,
			                          userPriority, &tokens[functionNameIndex]);
		}
	}
	else
	{
		// Waiting on definition or building of this compile-time function
		ObjectReference newReference = {};
		newReference.type = ObjectReferenceResolutionType_Splice;
		newReference.tokens = &tokens;
		// Unlike function references, we want to reevaluate from the start of add-hook
		newReference.startIndex = startTokenIndex;
		newReference.context = context;
		// We don't need to splice, we need to set the variable. Create it anyways
		newReference.spliceOutput = new GeneratorOutput;

		const ObjectReferenceStatus* referenceStatus =
		    addObjectReference(environment, tokens[functionNameIndex], newReference);
		if (!referenceStatus)
		{
			ErrorAtToken(tokens[functionNameIndex],
			             "failed to create reference status (internal error)");
			return false;
		}

		// Succeed only because we know the resolver will come back to us
		return true;
	}

	ErrorAtToken(tokens[hookNameIndex],
	             "failed to set hook. Hook name not recognized or context mismatched. Available "
	             "hooks:\n\tpre-build (module only)\n\tpre-link\n\tpost-references-resolved\n");
	return false;
}

bool AddStringOptionsGenerator(EvaluatorEnvironment& environment, const EvaluatorContext& context,
                               const std::vector<Token>& tokens, int startTokenIndex,
                               GeneratorOutput& output)
{
	// Don't let the user think this function can be called during comptime
	if (!ExpectEvaluatorScope("add string option", tokens[startTokenIndex], context,
	                          EvaluatorScope_Module))
	{
		NoteAtToken(
		    tokens[startTokenIndex],
		    "if you are trying to set an option conditionally from within a comptime function, you "
		    "should directly set the option rather than using this generator");
		return false;
	}

	const Token& invocationToken = tokens[startTokenIndex + 1];

	struct StringOptionList
	{
		const char* name;
		std::vector<std::string>* stringList;
	};
	const StringOptionList possibleDestinations[] = {
	    {"add-cakelisp-search-directory", &environment.searchPaths},
	    {"add-c-search-directory-global", &environment.cSearchDirectories},
	    {"add-c-search-directory-module", &context.module->cSearchDirectories},
	    {"add-library-search-directory", &context.module->librarySearchDirectories},
	    {"add-library-runtime-search-directory", &context.module->libraryRuntimeSearchDirectories},
	    {"add-library-dependency", &context.module->libraryDependencies},
	    {"add-compiler-link-options", &context.module->compilerLinkOptions},
	    {"add-linker-options", &context.module->toLinkerOptions},
	    {"add-static-link-objects", &environment.additionalStaticLinkObjects},
	    {"add-build-options", &context.module->additionalBuildOptions},
	    {"add-build-options-global", &environment.compilerAdditionalOptions},
	    {"add-build-config-label", &environment.buildConfigurationLabels}};

	const StringOptionList* destination = nullptr;
	for (unsigned int i = 0; i < ArraySize(possibleDestinations); ++i)
	{
		if (invocationToken.contents.compare(possibleDestinations[i].name) == 0)
		{
			destination = &possibleDestinations[i];
			break;
		}
	}

	if (!destination)
	{
		ErrorAtToken(invocationToken,
		             "unrecognized string option destination. Available destinations:");
		for (unsigned int i = 0; i < ArraySize(possibleDestinations); ++i)
			Logf("\t%s\n", possibleDestinations[i].name);

		return false;
	}

	int endInvocationIndex = FindCloseParenTokenIndex(tokens, startTokenIndex);

	int startStringsIndex =
	    getExpectedArgument("expected string(s)", tokens, startTokenIndex, 1, endInvocationIndex);
	if (startStringsIndex == -1)
		return false;

	for (int i = startStringsIndex; i < endInvocationIndex;
	     i = getNextArgument(tokens, i, endInvocationIndex))
	{
		const Token& currentToken = tokens[i];
		if (!ExpectTokenType("add string options", currentToken, TokenType_String))
			return false;

		bool found = false;
		for (const std::string& existingValue : *destination->stringList)
		{
			if (currentToken.contents.compare(existingValue) == 0)
			{
				found = true;
				break;
			}
		}
		if (!found)
		{
			destination->stringList->push_back(currentToken.contents);

			if (logging.optionAdding)
				NoteAtTokenf(currentToken, "added option %s (%s)", currentToken.contents.c_str(),
				             destination->name);
		}
	}

	return true;
}

// Only adds additional validation before AddStringOptionsGenerator()
bool AddBuildConfigLabelGenerator(EvaluatorEnvironment& environment,
                                  const EvaluatorContext& context, const std::vector<Token>& tokens,
                                  int startTokenIndex, GeneratorOutput& output)
{
	// Don't let the user think this function can be called during comptime
	if (!ExpectEvaluatorScope("add-build-config-label", tokens[startTokenIndex], context,
	                          EvaluatorScope_Module))
		return false;

	if (environment.buildConfigurationLabelsAreFinal)
	{
		ErrorAtToken(tokens[startTokenIndex],
		             "build configuration labels are finalized. No changes are accepted because "
		             "output is already being written");
		return false;
	}

	return AddStringOptionsGenerator(environment, context, tokens, startTokenIndex, output);
}

bool SkipBuildGenerator(EvaluatorEnvironment& environment, const EvaluatorContext& context,
                        const std::vector<Token>& tokens, int startTokenIndex,
                        GeneratorOutput& output)
{
	// Don't let the user think this function can be called during comptime
	if (!ExpectEvaluatorScope("skip-build", tokens[startTokenIndex], context,
	                          EvaluatorScope_Module))
		return false;

	if (context.module)
		context.module->skipBuild = true;
	else
	{
		ErrorAtToken(tokens[startTokenIndex], "building not supported (internal code error?)");
		return false;
	}

	return true;
}

// Allows users to rename built-in generators, making it possible to then define macros or
// generators as replacements.
bool RenameBuiltinGenerator(EvaluatorEnvironment& environment, const EvaluatorContext& context,
                            const std::vector<Token>& tokens, int startTokenIndex,
                            GeneratorOutput& output)
{
	int endInvocationIndex = FindCloseParenTokenIndex(tokens, startTokenIndex);
	int nameIndex = getExpectedArgument("expected built-in name", tokens, startTokenIndex, 1,
	                                    endInvocationIndex);
	if (nameIndex == -1)
		return false;

	int newNameIndex = getExpectedArgument("expected new name for builtin", tokens, startTokenIndex,
	                                       2, endInvocationIndex);
	if (newNameIndex == -1)
		return false;

	if (!ExpectTokenType("rename-builtin", tokens[nameIndex], TokenType_String) ||
	    !ExpectTokenType("rename-builtin", tokens[newNameIndex], TokenType_String))
		return false;

	// Don't re-rename it; it might be a user's function at this point
	GeneratorIterator findRenamedIt =
	    environment.renamedGenerators.find(tokens[nameIndex].contents);
	bool alreadyRenamed = findRenamedIt != environment.renamedGenerators.end();
	if (alreadyRenamed)
		return true;

	GeneratorIterator findIt = environment.generators.find(tokens[nameIndex].contents);
	if (findIt == environment.generators.end())
	{
		if (!alreadyRenamed)
		{
			ErrorAtToken(tokens[nameIndex], "built-in generator not found");
			return false;
		}

		// Already renamed
		return true;
	}

	// TODO: Go back and reevaluate all places where the old version was used?
	GeneratorLastReferenceTableIterator findReferenceIt =
	    environment.lastGeneratorReferences.find(tokens[nameIndex].contents);
	if (findReferenceIt != environment.lastGeneratorReferences.end())
	{
		ErrorAtToken(*findReferenceIt->second,
		             "rename-builtin: found reference to generator built-in before it could be "
		             "renamed. It is expected to rename built-ins before ever referenced, else "
		             "invocations evaluated before the rename will have different output");
		return false;
	}

	GeneratorFunc generator = findIt->second;
	environment.generators.erase(findIt);
	environment.generators[tokens[newNameIndex].contents] = generator;
	environment.renamedGenerators[tokens[nameIndex].contents] = generator;

	return true;
}

enum ImportState
{
	WithDefinitions,
	WithDeclarations,
	CompTimeOnly,
	DeclarationsOnly,
	// TODO: Remove?
	DefinitionsOnly
};

bool ImportGenerator(EvaluatorEnvironment& environment, const EvaluatorContext& context,
                     const std::vector<Token>& tokens, int startTokenIndex, GeneratorOutput& output)
{
	if (!ExpectEvaluatorScope("import", tokens[startTokenIndex], context, EvaluatorScope_Module))
		return false;

	int endTokenIndex = FindCloseParenTokenIndex(tokens, startTokenIndex);
	// Generators receive the entire invocation. We'll ignore it in this case
	int startNameTokenIndex = startTokenIndex;
	int endArgsIndex = endTokenIndex;
	StripInvocation(startNameTokenIndex, endArgsIndex);
	if (!ExpectInInvocation("expected path(s) to modules to import", tokens, startNameTokenIndex,
	                        endTokenIndex))
		return false;

	// C/C++ imports are "c-import"
	bool isCakeImport = tokens[startTokenIndex + 1].contents.compare("import") == 0;

	ImportState state = WithDefinitions;

	for (int i = startNameTokenIndex; i <= endArgsIndex; ++i)
	{
		const Token& currentToken = tokens[i];

		if (currentToken.type == TokenType_Symbol && isSpecialSymbol(currentToken))
		{
			if (currentToken.contents.compare("&with-defs") == 0)
				state = WithDefinitions;
			else if (currentToken.contents.compare("&decls-only") == 0)
			{
				if (!isCakeImport)
				{
					ErrorAtToken(currentToken, "&decls-only not supported on C/C++ imports");
					return false;
				}
				state = DeclarationsOnly;
			}
			else if (currentToken.contents.compare("&defs-only") == 0)
			{
				if (!isCakeImport)
				{
					ErrorAtToken(currentToken, "&defs-only not supported on C/C++ imports");
					return false;
				}
				state = DefinitionsOnly;
			}
			else if (currentToken.contents.compare("&with-decls") == 0)
				state = WithDeclarations;
			else if (currentToken.contents.compare("&comptime-only") == 0)
			{
				if (!isCakeImport)
				{
					ErrorAtToken(currentToken, "&comptime-only not supported on C/C++ imports");
					return false;
				}
				state = CompTimeOnly;
			}
			else
			{
				ErrorAtToken(currentToken,
				             "Unrecognized sentinel symbol. Options "
				             "are:\n\t&with-defs\n\t&with-decls\n\t&decls-only\n\t&defs-only\n\t&"
				             "comptime-only\n");
				return false;
			}

			continue;
		}
		else if (!ExpectTokenType("import file", currentToken, TokenType_String) ||
		         currentToken.contents.empty())
			return false;

		Module* importedModule = nullptr;
		if (isCakeImport)
		{
			if (!environment.moduleManager)
			{
				ErrorAtToken(currentToken,
				             "importing Cakelisp modules is disabled in this environment");
				return false;
			}
			else
			{
				if (context.module)
				{
					ModuleDependency newCakelispDependency = {};
					newCakelispDependency.type = ModuleDependency_Cakelisp;
					newCakelispDependency.name = currentToken.contents;
					context.module->dependencies.push_back(newCakelispDependency);
				}
				else
				{
					NoteAtToken(currentToken,
					            "module cannot track dependency (potential internal error)");
				}

				char resolvedPathBuffer[MAX_PATH_LENGTH] = {0};
				if (!searchForFileInPathsWithError(currentToken.contents.c_str(),
				                                   /*encounteredInFile=*/currentToken.source,
				                                   environment.searchPaths, resolvedPathBuffer,
				                                   ArraySize(resolvedPathBuffer), currentToken))
					return false;

				// Evaluate the import! Will only evaluate it on first import in this environment
				if (!moduleManagerAddEvaluateFile(*environment.moduleManager, resolvedPathBuffer,
				                                  &importedModule))
				{
					ErrorAtToken(currentToken, "failed to import Cakelisp module");
					return false;
				}

				// Either we only want this file for its header or its macros. Don't build it into
				// the runtime library/executable
				if ((state == DeclarationsOnly || state == CompTimeOnly) && importedModule)
				{
					// TODO: This won't protect us from a module changing the environment, which may
					// not be desired
					importedModule->skipBuild = true;
				}
			}
		}

		// Comptime only means no includes in the generated file
		bool shouldIncludeHeader = state != CompTimeOnly && state != DefinitionsOnly;
		if (shouldIncludeHeader)
		{
			std::vector<StringOutput>& outputDestination =
			    state == WithDefinitions ? output.source : output.header;

			// #include <stdio.h> is passed in as "<stdio.h>", so we need a special case (no quotes)
			if (currentToken.contents[0] == '<')
			{
				addStringOutput(outputDestination, "#include", StringOutMod_SpaceAfter,
				                &currentToken);
				addStringOutput(outputDestination, currentToken.contents, StringOutMod_None,
				                &currentToken);
				addLangTokenOutput(outputDestination, StringOutMod_NewlineAfter, &currentToken);
			}
			else
			{
				if (isCakeImport)
				{
					// Defer the import until we know what language requirements it has and whether
					// it even needs to be imported (e.g., whether it's all macros, so it would
					// generate no runtime header)
					CakelispDeferredImport newCakelispImport;
					newCakelispImport.fileToImportToken = &currentToken;
					// TODO: Should be an easy add for outputting to both
					newCakelispImport.outputTo = state == WithDeclarations ?
					                                 CakelispImportOutput_Header :
					                                 CakelispImportOutput_Source;
					newCakelispImport.spliceOutput = new GeneratorOutput;
					newCakelispImport.importedModule = importedModule;
					addSpliceOutput(output, newCakelispImport.spliceOutput, &currentToken);
					context.module->cakelispImports.push_back(newCakelispImport);
				}
				else
				{
					addStringOutput(outputDestination, "#include", StringOutMod_SpaceAfter,
					                &currentToken);
					addStringOutput(outputDestination, currentToken.contents,
					                StringOutMod_SurroundWithQuotes, &currentToken);
					addLangTokenOutput(outputDestination, StringOutMod_NewlineAfter, &currentToken);
				}
			}
		}

		// Evaluate the import's exports in the current context (the importer module)
		if (importedModule)
		{
			// We have imported the file. Prevent new exports being created to simplify things
			importedModule->exportScopesLocked = true;

			for (ModuleExportScope& exportScope : importedModule->exportScopes)
			{
				// Already processed this export. Is this even necessary?
				if (exportScope.modulesEvaluatedExport.find(context.module->filename) !=
				    exportScope.modulesEvaluatedExport.end())
					continue;

				int startEvalateTokenIndex =
				    exportScope.startTokenIndex + EXPORT_SCOPE_START_EVAL_OFFSET;

				EvaluatorContext exportModuleContext = context;
				int numErrors = EvaluateGenerateAll_Recursive(environment, exportModuleContext,
				                                              *exportScope.tokens,
				                                              startEvalateTokenIndex, output);
				if (numErrors)
				{
					NoteAtToken((*exportScope.tokens)[exportScope.startTokenIndex],
					            "while evaluating export");
					NoteAtToken(tokens[startTokenIndex], "export came from this import");
					return false;
				}

				exportScope.modulesEvaluatedExport[context.module->filename] = 1;
			}
		}

		output.imports.push_back({currentToken.contents,
		                          isCakeImport ? ImportLanguage_Cakelisp : ImportLanguage_C,
		                          &currentToken});
	}

	return true;
}

bool AddDependencyGenerator(EvaluatorEnvironment& environment, const EvaluatorContext& context,
                            const std::vector<Token>& tokens, int startTokenIndex,
                            GeneratorOutput& output)
{
	// Don't let the user think this function can be called during comptime
	if (!ExpectEvaluatorScope("add-c/cpp-build-dependency", tokens[startTokenIndex], context,
	                          EvaluatorScope_Module))
		return false;

	if (!context.module)
	{
		ErrorAtToken(tokens[startTokenIndex],
		             "module cannot track dependency (potential internal error)");
		return false;
	}

	int endInvocationIndex = FindCloseParenTokenIndex(tokens, startTokenIndex);

	int firstNameIndex = getExpectedArgument("expected dependency name", tokens, startTokenIndex, 1,
	                                         endInvocationIndex);
	if (firstNameIndex == -1)
		return false;

	for (int i = firstNameIndex; i < endInvocationIndex;
	     i = getNextArgument(tokens, i, endInvocationIndex))
	{
		const Token& currentDependencyName = tokens[i];
		if (!ExpectTokenType("add dependency", currentDependencyName, TokenType_String))
			return false;

		ModuleDependency newDependency = {};
		newDependency.type = ModuleDependency_CFile;
		// The full name will be resolved at build time
		newDependency.name = currentDependencyName.contents;
		newDependency.blameToken = &currentDependencyName;
		context.module->dependencies.push_back(newDependency);
	}

	return true;
}

bool CPreprocessorDefineGenerator(EvaluatorEnvironment& environment,
                                  const EvaluatorContext& context, const std::vector<Token>& tokens,
                                  int startTokenIndex, GeneratorOutput& output)
{
	if (IsForbiddenEvaluatorScope("c-preprocessor-define", tokens[startTokenIndex], context,
	                              EvaluatorScope_ExpressionsOnly))
		return false;

	int endInvocationIndex = FindCloseParenTokenIndex(tokens, startTokenIndex);
	int defineNameIndex =
	    getExpectedArgument("define-name", tokens, startTokenIndex, 1, endInvocationIndex);
	if (-1 == defineNameIndex)
		return false;
	if (!ExpectTokenType("define-name", tokens[defineNameIndex], TokenType_Symbol))
		return false;
	const Token* defineName = &tokens[defineNameIndex];
	int valueIndex = getArgument(tokens, startTokenIndex, 2, endInvocationIndex);
	const Token* value = valueIndex != -1 ? &tokens[valueIndex] : nullptr;

	bool isGlobal =
	    tokens[startTokenIndex + 1].contents.compare("c-preprocessor-define-global") == 0;

	std::vector<StringOutput>& outputDest = isGlobal ? output.header : output.source;

	if (value)
	{
		addStringOutput(outputDest, "#define", StringOutMod_SpaceAfter, &tokens[startTokenIndex]);
		addStringOutput(outputDest, defineName->contents, StringOutMod_SpaceAfter, defineName);
		if (value->type == TokenType_String)
			addStringOutput(outputDest, value->contents,
			                (StringOutputModifierFlags)(StringOutMod_NewlineAfter |
			                                            StringOutMod_SurroundWithQuotes),
			                value);
		else
			addStringOutput(outputDest, value->contents, StringOutMod_NewlineAfter, value);
	}
	else
	{
		addStringOutput(outputDest, "#define", StringOutMod_SpaceAfter, &tokens[startTokenIndex]);
		addStringOutput(outputDest, defineName->contents, StringOutMod_NewlineAfter, defineName);
	}
	return true;
}

bool DefunGenerator(EvaluatorEnvironment& environment, const EvaluatorContext& context,
                    const std::vector<Token>& tokens, int startTokenIndex, GeneratorOutput& output)
{
	if (!ExpectEvaluatorScope("defun", tokens[startTokenIndex], context, EvaluatorScope_Module))
		return false;

	int endInvocationTokenIndex = FindCloseParenTokenIndex(tokens, startTokenIndex);
	int endTokenIndex = endInvocationTokenIndex;
	int startNameTokenIndex = startTokenIndex;
	StripInvocation(startNameTokenIndex, endTokenIndex);

	int nameIndex = startNameTokenIndex;
	const Token& nameToken = tokens[nameIndex];
	if (!ExpectTokenType("defun", nameToken, TokenType_Symbol))
		return false;

	int argsIndex = nameIndex + 1;
	if (!ExpectInInvocation("defun expected arguments", tokens, argsIndex, endInvocationTokenIndex))
		return false;
	const Token& argsStart = tokens[argsIndex];
	if (!ExpectTokenType("defun", argsStart, TokenType_OpenParen))
		return false;

	bool isModuleLocal = tokens[startTokenIndex + 1].contents.compare("defun-local") == 0;
	bool isNoDeclare = tokens[startTokenIndex + 1].contents.compare("defun-nodecl") == 0;
	bool shouldDeclare = !isModuleLocal && !isNoDeclare;
	// Note that macros and generators have their own generators, so we don't handle them here
	bool isCompileTime = tokens[startTokenIndex + 1].contents.compare("defun-comptime") == 0;

	// In order to support function definition modification, even runtime functions must have
	// spliced output, because we might be completely changing the definition
	GeneratorOutput* functionOutput = new GeneratorOutput;

	// Register definition before evaluating body, otherwise references in body will be orphaned
	{
		ObjectDefinition newFunctionDef = {};
		newFunctionDef.definitionInvocation = &tokens[startTokenIndex];
		newFunctionDef.name = nameToken.contents.c_str();
		newFunctionDef.type = isCompileTime ? ObjectType_CompileTimeFunction : ObjectType_Function;
		// Compile-time objects only get built with compile-time references
		newFunctionDef.isRequired = isCompileTime ? false : context.isRequired;
		newFunctionDef.context = context;
		newFunctionDef.output = functionOutput;
		if (!addObjectDefinition(environment, newFunctionDef))
		{
			delete functionOutput;
			return false;
		}
		// Past this point, compile-time output will be handled by environment destruction

		// Regardless of how much the definition is modified, it will still be output at this place
		// in the module's generated file. Compile-time functions don't splice into module because
		// they shouldn't be included in runtime code
		if (!isCompileTime)
			addSpliceOutput(output, functionOutput, &tokens[startTokenIndex]);

		if (isCompileTime)
		{
			CompileTimeFunctionMetadata newMetadata = {};
			newMetadata.nameToken = &nameToken;
			newMetadata.startArgsToken = &argsStart;
			environment.compileTimeFunctionInfo[nameToken.contents.c_str()] = newMetadata;
		}
	}

	int returnTypeStart = -1;
	int isVariadicIndex = -1;
	std::vector<FunctionArgumentTokens> arguments;
	if (!parseFunctionSignature(tokens, argsIndex, arguments, returnTypeStart, isVariadicIndex))
		return false;

	if (isCompileTime && environment.isMsvcCompiler)
	{
		addStringOutput(functionOutput->source, "__declspec(dllexport)", StringOutMod_SpaceAfter,
		                &tokens[startTokenIndex]);
		addStringOutput(functionOutput->header, "extern \"C\" __declspec(dllimport)",
		                StringOutMod_SpaceAfter, &tokens[startTokenIndex]);
		// addStringOutput(functionOutput->header, "__declspec(dllimport)", StringOutMod_SpaceAfter,
		// &tokens[startTokenIndex]);
	}
	// Compile-time functions need to be exposed with C bindings so they can be found (true?)
	// Module-local functions are always marked static, which hides them from linking
	else if (shouldDeclare && (isCompileTime || environment.useCLinkage))
	{
		addStringOutput(functionOutput->header,
		                "\n#ifdef __cplusplus\n"
		                "extern \"C\"\n"
		                "#endif",
		                StringOutMod_NewlineAfter, &tokens[startTokenIndex]);
	}

	if (isModuleLocal)
		addStringOutput(functionOutput->source, "static", StringOutMod_SpaceAfter,
		                &tokens[startTokenIndex]);

	int endArgsIndex = FindCloseParenTokenIndex(tokens, argsIndex);
	if (!outputFunctionReturnType(environment, context, tokens, *functionOutput, returnTypeStart,
	                              startTokenIndex, endArgsIndex,
	                              /*outputSource=*/true, /*outputHeader=*/shouldDeclare))
		return false;

	addStringOutput(functionOutput->source, nameToken.contents, StringOutMod_ConvertFunctionName,
	                &nameToken);
	if (shouldDeclare)
		addStringOutput(functionOutput->header, nameToken.contents,
		                StringOutMod_ConvertFunctionName, &nameToken);

	addLangTokenOutput(functionOutput->source, StringOutMod_OpenParen, &argsStart);
	if (shouldDeclare)
		addLangTokenOutput(functionOutput->header, StringOutMod_OpenParen, &argsStart);

	if (!outputFunctionArguments(environment, context, tokens, *functionOutput, arguments,
	                             isVariadicIndex,
	                             /*outputSource=*/true,
	                             /*outputHeader=*/shouldDeclare))
		return false;

	std::vector<FunctionArgumentMetadata> argumentsMetadata;
	for (FunctionArgumentTokens& arg : arguments)
	{
		const Token* startTypeToken = &tokens[arg.startTypeIndex];
		const Token* endTypeToken = startTypeToken;
		if (startTypeToken->type == TokenType_OpenParen)
			endTypeToken = &tokens[FindCloseParenTokenIndex(tokens, arg.startTypeIndex)];

		argumentsMetadata.push_back({tokens[arg.nameIndex].contents, startTypeToken, endTypeToken});
	}

	addLangTokenOutput(functionOutput->source, StringOutMod_CloseParen, &tokens[endArgsIndex]);
	if (shouldDeclare)
	{
		addLangTokenOutput(functionOutput->header, StringOutMod_CloseParen, &tokens[endArgsIndex]);
		// Forward declarations end with ;
		addLangTokenOutput(functionOutput->header, StringOutMod_EndStatement,
		                   &tokens[endArgsIndex]);
	}

	int startBodyIndex = endArgsIndex + 1;
	addLangTokenOutput(functionOutput->source, StringOutMod_OpenScopeBlock,
	                   &tokens[startBodyIndex]);

	// Evaluate our body!
	EvaluatorContext bodyContext = context;
	bodyContext.scope = EvaluatorScope_Body;
	bodyContext.definitionName = &nameToken;
	bodyContext.delimiterTemplate = {};
	// The statements will need to handle their ;
	int numErrors = EvaluateGenerateAll_Recursive(environment, bodyContext, tokens, startBodyIndex,
	                                              *functionOutput);
	if (numErrors)
	{
		// Don't delete compile time function output. Evaluate may have caused references to it
		return false;
	}

	addLangTokenOutput(functionOutput->source, StringOutMod_CloseScopeBlock,
	                   &tokens[endTokenIndex]);

	functionOutput->functions.push_back({nameToken.contents, &tokens[startTokenIndex],
	                                     &tokens[endTokenIndex], std::move(argumentsMetadata)});

	return true;
}

bool DefFunctionSignatureGenerator(EvaluatorEnvironment& environment,
                                   const EvaluatorContext& context,
                                   const std::vector<Token>& tokens, int startTokenIndex,
                                   GeneratorOutput& output)
{
	if (IsForbiddenEvaluatorScope("def-function-signature", tokens[startTokenIndex], context,
	                              EvaluatorScope_ExpressionsOnly))
		return false;

	int endInvocationTokenIndex = FindCloseParenTokenIndex(tokens, startTokenIndex);
	int endTokenIndex = endInvocationTokenIndex;
	int startNameTokenIndex = startTokenIndex;
	StripInvocation(startNameTokenIndex, endTokenIndex);

	int nameIndex = startNameTokenIndex;
	const Token& nameToken = tokens[nameIndex];
	if (!ExpectTokenType("def-function-signature", nameToken, TokenType_Symbol))
		return false;

	int argsIndex = nameIndex + 1;
	if (!ExpectInInvocation("def-function-signature expected arguments", tokens, argsIndex,
	                        endInvocationTokenIndex))
		return false;
	const Token& argsStart = tokens[argsIndex];
	if (!ExpectTokenType("def-function-signature", argsStart, TokenType_OpenParen))
		return false;

	bool isModuleLocal =
	    tokens[startTokenIndex + 1].contents.compare("def-function-signature-global") != 0;

	if (!isModuleLocal && context.scope != EvaluatorScope_Module)
	{
		ErrorAtToken(tokens[startTokenIndex + 1],
		             "cannot specify global if in body or expression scope. Move it to top-level "
		             "module scope");
		return false;
	}

	std::vector<StringOutput>& outputDest = isModuleLocal ? output.source : output.header;

	int returnTypeStart = -1;
	int isVariadicIndex = -1;
	std::vector<FunctionArgumentTokens> arguments;
	if (!parseFunctionSignature(tokens, argsIndex, arguments, returnTypeStart, isVariadicIndex))
		return false;

	addStringOutput(outputDest, "typedef", StringOutMod_SpaceAfter, &tokens[startTokenIndex]);

	int endArgsIndex = FindCloseParenTokenIndex(tokens, argsIndex);
	if (!outputFunctionReturnType(environment, context, tokens, output, returnTypeStart,
	                              startTokenIndex, endArgsIndex,
	                              /*outputSource=*/isModuleLocal, /*outputHeader=*/!isModuleLocal))
		return false;

	// Name
	addLangTokenOutput(outputDest, StringOutMod_OpenParen, &nameToken);
	addStringOutput(outputDest, "*", StringOutMod_None, &nameToken);
	addStringOutput(outputDest, nameToken.contents, StringOutMod_ConvertTypeName, &nameToken);
	addLangTokenOutput(outputDest, StringOutMod_CloseParen, &nameToken);

	addLangTokenOutput(outputDest, StringOutMod_OpenParen, &argsStart);

	if (!outputFunctionArguments(environment, context, tokens, output, arguments, isVariadicIndex,
	                             /*outputSource=*/isModuleLocal,
	                             /*outputHeader=*/!isModuleLocal))
		return false;

	addLangTokenOutput(outputDest, StringOutMod_CloseParen, &tokens[endArgsIndex]);
	addLangTokenOutput(outputDest, StringOutMod_EndStatement, &tokens[endArgsIndex]);

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

	addStringOutput(output.source, funcNameToken.contents, StringOutMod_ConvertFunctionName,
	                &funcNameToken);
	addLangTokenOutput(output.source, StringOutMod_OpenParen, &funcNameToken);

	// Arguments
	int startArgsIndex = nameTokenIndex + 1;

	// Function invocations evaluate their arguments
	EvaluatorContext functionInvokeContext = context;
	functionInvokeContext.scope = EvaluatorScope_ExpressionsOnly;
	StringOutput argumentDelimiterTemplate = {};
	argumentDelimiterTemplate.modifiers = StringOutMod_ListSeparator;
	functionInvokeContext.delimiterTemplate = argumentDelimiterTemplate;
	int numErrors = EvaluateGenerateAll_Recursive(environment, functionInvokeContext, tokens,
	                                              startArgsIndex, output);
	if (numErrors)
		return false;

	addLangTokenOutput(output.source, StringOutMod_CloseParen, &tokens[endInvocationIndex]);
	if (context.scope != EvaluatorScope_ExpressionsOnly)
		addLangTokenOutput(output.source, StringOutMod_EndStatement, &tokens[endInvocationIndex]);

	return true;
}

// Handles both uninitialized and initialized variables as well as global and static
// Module-local variables are automatically marked as static
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
	bool isGlobal = funcNameToken.contents.compare("var-global") == 0;
	if (isGlobal && !ExpectEvaluatorScope("global variable declaration", tokens[startTokenIndex],
	                                      context, EvaluatorScope_Module))
		return false;

	// Only necessary for static variables declared inside a function
	bool isStatic = funcNameToken.contents.compare("var-static") == 0;
	if (isStatic && !ExpectEvaluatorScope("static variable declaration", tokens[startTokenIndex],
	                                      context, EvaluatorScope_Body))
		return false;

	int varNameIndex = getExpectedArgument("expected variable name", tokens, startTokenIndex, 1,
	                                       endInvocationIndex);
	if (varNameIndex == -1)
		return false;

	int typeIndex = getExpectedArgument("expected variable type", tokens, startTokenIndex, 2,
	                                    endInvocationIndex);
	if (typeIndex == -1)
		return false;

	std::vector<StringOutput> typeOutput;
	std::vector<StringOutput> typeAfterNameOutput;
	// Arrays cannot be return types, they must be * instead
	if (!tokenizedCTypeToString_Recursive(environment, context, tokens, typeIndex,
	                                      /*allowArray=*/true, typeOutput, typeAfterNameOutput))
		return false;

	// At this point, we probably have a valid variable. Start outputting
	addModifierToStringOutput(typeOutput.back(), StringOutMod_SpaceAfter);

	GeneratorOutput* variableOutput = &output;

	// Create separate output and definition to support compile-time modification
	// For now, don't bother with variables in functions
	if (context.scope == EvaluatorScope_Module)
	{
		variableOutput = new GeneratorOutput;
		{
			ObjectDefinition newVariableDef = {};
			newVariableDef.definitionInvocation = &tokens[startTokenIndex];
			newVariableDef.name = tokens[varNameIndex].contents.c_str();
			newVariableDef.type = ObjectType_Variable;
			// Required only relevant for compile-time things
			newVariableDef.isRequired = false;
			newVariableDef.context = context;
			newVariableDef.output = variableOutput;
			if (!addObjectDefinition(environment, newVariableDef))
			{
				delete variableOutput;
				return false;
			}
			// Past this point, output will be handled by environment destruction

			// Regardless of how much the definition is modified, it will still be output at this
			// place in the module's generated file
			addSpliceOutput(output, variableOutput, &tokens[startTokenIndex]);
		}
	}

	if (isGlobal)
		addStringOutput(variableOutput->header, "extern", StringOutMod_SpaceAfter,
		                &tokens[startTokenIndex]);
	// else because no variable may be declared extern while also being static
	// Automatically make module-declared variables static, reserving "static-var" for functions
	else if (isStatic || context.scope == EvaluatorScope_Module)
		addStringOutput(variableOutput->source, "static", StringOutMod_SpaceAfter,
		                &tokens[startTokenIndex]);

	// Type
	PushBackAll(variableOutput->source, typeOutput);
	if (isGlobal)
		PushBackAll(variableOutput->header, typeOutput);

	// Name
	addStringOutput(variableOutput->source, tokens[varNameIndex].contents,
	                StringOutMod_ConvertVariableName, &tokens[varNameIndex]);
	if (isGlobal)
		addStringOutput(variableOutput->header, tokens[varNameIndex].contents,
		                StringOutMod_ConvertVariableName, &tokens[varNameIndex]);

	// Array
	PushBackAll(variableOutput->source, typeAfterNameOutput);
	if (isGlobal)
		PushBackAll(variableOutput->header, typeAfterNameOutput);

	// Possibly find whether it is initialized
	int valueIndex = getNextArgument(tokens, typeIndex, endInvocationIndex);

	// Initialized
	if (valueIndex < endInvocationIndex)
	{
		addLangTokenOutput(variableOutput->source, StringOutMod_SpaceAfter, &tokens[valueIndex]);
		addStringOutput(variableOutput->source, "=", StringOutMod_SpaceAfter, &tokens[valueIndex]);

		EvaluatorContext expressionContext = context;
		expressionContext.scope = EvaluatorScope_ExpressionsOnly;
		if (EvaluateGenerate_Recursive(environment, expressionContext, tokens, valueIndex,
		                               *variableOutput) != 0)
			return false;

		if (!ExpectNumArguments(tokens, startTokenIndex, endInvocationIndex, 4))
			return false;
	}
	else
	{
		if (!ExpectNumArguments(tokens, startTokenIndex, endInvocationIndex, 3))
			return false;
	}

	addLangTokenOutput(variableOutput->source, StringOutMod_EndStatement,
	                   &tokens[endInvocationIndex]);
	if (isGlobal)
		addLangTokenOutput(variableOutput->header, StringOutMod_EndStatement,
		                   &tokens[endInvocationIndex]);

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
		addStringOutput(output.source, "[", StringOutMod_None, &tokens[offsetTokenIndex]);
		if (EvaluateGenerate_Recursive(environment, expressionContext, tokens, offsetTokenIndex,
		                               output) != 0)
			return false;
		addStringOutput(output.source, "]", StringOutMod_None, &tokens[offsetTokenIndex]);
	}

	return true;
}

enum ComptimeTokenArgContainedType
{
	ExpectTokenType_Unrecognized,
	ExpectTokenType_Any,
	ExpectTokenType_String,
	ExpectTokenType_Symbol,
	ExpectTokenType_Array,  // A list in lisp, but in Cakelisp, there are no linked lists
};
static ComptimeTokenArgContainedType ComptimeParseTokenArgumentType(const Token& expectedType)
{
	ComptimeTokenArgContainedType type = ExpectTokenType_Unrecognized;

	if (!ExpectTokenType("token argument type", expectedType, TokenType_Symbol))
		return type;

	struct
	{
		const char* keyword;
		ComptimeTokenArgContainedType type;
	} containedTypeOptions[] = {{"any", ExpectTokenType_Any},
	                            {"string", ExpectTokenType_String},
	                            {"symbol", ExpectTokenType_Symbol},
	                            {"array", ExpectTokenType_Array}};
	bool foundType = false;
	for (unsigned int i = 0; i < ArraySize(containedTypeOptions); ++i)
	{
		if (expectedType.contents.compare(containedTypeOptions[i].keyword) == 0)
		{
			type = containedTypeOptions[i].type;
			foundType = true;
			break;
		}
	}

	if (!foundType)
	{
		ErrorAtToken(expectedType, "unrecognized type. Recognized types listed below.");
		for (unsigned int i = 0; i < ArraySize(containedTypeOptions); ++i)
		{
			Logf("\t%s\n", containedTypeOptions[i].keyword);
		}
	}

	return type;
}

// A domain-specific language for easily extracting and validating tokens from token array
// Comptime functions have hard-coded signatures, so "arguments" are actually extracted in the body
// Wow, this is brutal. Great feature, ugly implementation
static bool ComptimeGenerateTokenArguments(const std::vector<Token>& tokens, int startArgsIndex,
                                           GeneratorOutput& output)
{
	if (!ExpectTokenType("token arguments", tokens[startArgsIndex], TokenType_OpenParen))
		return false;

	int endArgsTokenIndex = FindCloseParenTokenIndex(tokens, startArgsIndex);
	if (startArgsIndex + 1 == endArgsTokenIndex)
	{
		// No specified arguments; nothing to do
		return true;
	}

	addStringOutput(output.source,
	                "int AutoArgs_endInvocationIndex = FindCloseParenTokenIndex(tokens, "
	                "startTokenIndex);",
	                StringOutMod_NewlineAfter, &tokens[startArgsIndex]);

	enum ArgReadState
	{
		Name,
		Type
	};
	ArgReadState state = Name;

	struct TokenArgument
	{
		const Token* name;
	};

	TokenArgument argument = {0};
	bool isOptional = false;
	bool checkArgCount = true;
	int runtimeArgumentIndex = 1;  // Invocation = 0
	int numRequiredArguments = 1;  // Invocation counts as one

	for (int currentArgIndex = startArgsIndex + 1; currentArgIndex < endArgsTokenIndex;
	     currentArgIndex = getNextArgument(tokens, currentArgIndex, endArgsTokenIndex))
	{
		const Token& currentToken = tokens[currentArgIndex];

		if (state == Name)
		{
			if (currentToken.type == TokenType_Symbol && isSpecialSymbol(currentToken))
			{
				if (currentToken.contents.compare("&optional") == 0)
				{
					isOptional = true;
					continue;
				}
				else if (currentToken.contents.compare("&rest") == 0)
				{
					checkArgCount = false;
					continue;
				}
				else
				{
					ErrorAtToken(currentToken,
					             "unrecognized argument mode. Recognized types: &optional, &rest");
					return false;
				}
			}

			if (!ExpectTokenType("token argument name", currentToken, TokenType_Symbol))
				return false;

			argument.name = &currentToken;
			state = Type;
		}
		else if (state == Type)
		{
			enum TokenBindType
			{
				ArgumentIndex,
				Index,
				Pointer,
				Reference,
			};

			TokenBindType bindType = Pointer;
			ComptimeTokenArgContainedType containedType = ExpectTokenType_Unrecognized;
			if (currentToken.type == TokenType_OpenParen)
			{
				const Token& typeModifier = tokens[currentArgIndex + 1];
				if (typeModifier.contents.compare("arg-index") == 0)
					bindType = ArgumentIndex;
				else if (typeModifier.contents.compare("index") == 0)
					bindType = Index;
				else if (typeModifier.contents.compare("ref") == 0)
					bindType = Reference;
				else
				{
					ErrorAtToken(typeModifier,
					             "unrecognized type. Recognized types: index, reference (pointer "
					             "is implicit default)");
					return false;
				}

				const Token& containedTypeToken = tokens[currentArgIndex + 2];
				containedType = ComptimeParseTokenArgumentType(containedTypeToken);
			}
			else
			{
				containedType = ComptimeParseTokenArgumentType(currentToken);
			}

			if (containedType == ExpectTokenType_Unrecognized)
				return false;

			if (isOptional && bindType == Reference)
			{
				ErrorAtToken(currentToken,
				             "cannot bind reference to optional argument. It could be null, but "
				             "references may never be null. Use a pointer or index instead");
				return false;
			}

			NameStyleSettings nameStyle;
			char convertedName[MAX_NAME_LENGTH] = {0};
			lispNameStyleToCNameStyle(nameStyle.variableNameMode, argument.name->contents.c_str(),
			                          convertedName, sizeof(convertedName), *argument.name);

#define OutputIndexName()                                                                \
	{                                                                                    \
		addStringOutput(output.source, "AutoArgs_", StringOutMod_None, argument.name);   \
		addStringOutput(output.source, convertedName, StringOutMod_None, argument.name); \
		addStringOutput(output.source, "Index", StringOutMod_None, argument.name);       \
	}

			addStringOutput(output.source, "int", StringOutMod_SpaceAfter, argument.name);
			OutputIndexName();
			addStringOutput(
			    output.source, "=",
			    (StringOutputModifierFlags)(StringOutMod_SpaceAfter | StringOutMod_SpaceBefore),
			    argument.name);
			if (isOptional)
				addStringOutput(output.source, "getArgument(", StringOutMod_None, argument.name);
			else
			{
				addStringOutput(output.source, "getExpectedArgument(", StringOutMod_SpaceAfter,
				                argument.name);
				addStringOutput(output.source, argument.name->contents.c_str(),
				                StringOutMod_SurroundWithQuotes, argument.name);
				addLangTokenOutput(output.source, StringOutMod_ListSeparator, argument.name);
			}

			addStringOutput(output.source, "tokens, startTokenIndex,", StringOutMod_SpaceAfter,
			                argument.name);

			addStringOutput(output.source, std::to_string(runtimeArgumentIndex), StringOutMod_None,
			                argument.name);
			addLangTokenOutput(output.source, StringOutMod_ListSeparator, argument.name);
			addStringOutput(output.source, "AutoArgs_endInvocationIndex", StringOutMod_SpaceBefore,
			                argument.name);
			addLangTokenOutput(output.source, StringOutMod_CloseParen, argument.name);
			addLangTokenOutput(output.source, StringOutMod_EndStatement, argument.name);

			// Missing argument check
			if (!isOptional)
			{
				addStringOutput(output.source, "if (-1 ==", StringOutMod_SpaceAfter, argument.name);
				OutputIndexName();
				addStringOutput(output.source, ") return false", StringOutMod_None, argument.name);
				addLangTokenOutput(output.source, StringOutMod_EndStatement, argument.name);
			}

			// Expect type
			if (containedType != ExpectTokenType_Any)
			{
				addStringOutput(output.source, "if (", StringOutMod_None, argument.name);
				if (isOptional)
				{
					OutputIndexName();
					addStringOutput(output.source, "!= -1 &&",
					                (StringOutputModifierFlags)(StringOutMod_SpaceAfter |
					                                            StringOutMod_SpaceBefore),
					                argument.name);
				}
				addStringOutput(output.source, "!ExpectTokenType(", StringOutMod_None,
				                argument.name);

				addStringOutput(output.source, argument.name->contents.c_str(),
				                StringOutMod_SurroundWithQuotes, argument.name);
				addLangTokenOutput(output.source, StringOutMod_ListSeparator, argument.name);
				addStringOutput(output.source, "tokens[", StringOutMod_None, argument.name);

				OutputIndexName();
				addStringOutput(output.source, "],", StringOutMod_SpaceAfter, argument.name);

				switch (containedType)
				{
					case ExpectTokenType_String:
						addStringOutput(output.source, "TokenType_String", StringOutMod_None,
						                &currentToken);
						break;
					case ExpectTokenType_Symbol:
						addStringOutput(output.source, "TokenType_Symbol", StringOutMod_None,
						                &currentToken);
						break;
					case ExpectTokenType_Array:
						addStringOutput(output.source, "TokenType_OpenParen", StringOutMod_None,
						                &currentToken);
						break;
					default:
						Log("ComptimeGenerateTokenArguments: Programmer missing type. Internal "
						    "code error\n");
						return false;
				}

				addStringOutput(output.source, ")) return false", StringOutMod_None, argument.name);
				addLangTokenOutput(output.source, StringOutMod_EndStatement, argument.name);
			}

			// Finally, output the binding
			switch (bindType)
			{
				case ArgumentIndex:
					addStringOutput(output.source, "int", StringOutMod_SpaceAfter, argument.name);
					addStringOutput(output.source, convertedName, StringOutMod_SpaceAfter,
					                argument.name);
					addStringOutput(output.source, "=", StringOutMod_SpaceAfter, argument.name);
					if (isOptional)
					{
						OutputIndexName();
						addStringOutput(output.source, "!= -1 ?",
						                (StringOutputModifierFlags)(StringOutMod_SpaceAfter |
						                                            StringOutMod_SpaceBefore),
						                argument.name);
					}
					addStringOutput(output.source, std::to_string(runtimeArgumentIndex),
					                StringOutMod_None, argument.name);
					if (isOptional)
					{
						// It's a bit weird specifying optional on an argument index, but we will
						// support it via setting unspecified arguments to -1
						addStringOutput(output.source, " : -1", StringOutMod_None, argument.name);
					}
					addLangTokenOutput(output.source, StringOutMod_EndStatement, argument.name);
					break;
				case Index:
					addStringOutput(output.source, "int", StringOutMod_SpaceAfter, argument.name);
					addStringOutput(output.source, convertedName, StringOutMod_SpaceAfter,
					                argument.name);
					addStringOutput(output.source, "=", StringOutMod_SpaceAfter, argument.name);
					OutputIndexName();
					addLangTokenOutput(output.source, StringOutMod_EndStatement, argument.name);
					break;
				case Pointer:
					addStringOutput(output.source, "const Token*", StringOutMod_SpaceAfter,
					                argument.name);
					addStringOutput(output.source, convertedName, StringOutMod_SpaceAfter,
					                argument.name);
					addStringOutput(output.source, "=", StringOutMod_SpaceAfter, argument.name);
					if (isOptional)
					{
						OutputIndexName();
						addStringOutput(output.source, "!= -1 ?",
						                (StringOutputModifierFlags)(StringOutMod_SpaceAfter |
						                                            StringOutMod_SpaceBefore),
						                argument.name);
					}
					addStringOutput(output.source, "&tokens[", StringOutMod_None, argument.name);
					OutputIndexName();
					addStringOutput(output.source, "]", StringOutMod_None, argument.name);
					if (isOptional)
					{
						// Use 0 instead of worrying about nullptr vs NULL
						addStringOutput(output.source, ": 0", StringOutMod_None, argument.name);
					}
					addLangTokenOutput(output.source, StringOutMod_EndStatement, argument.name);
					break;
				case Reference:
					addStringOutput(output.source, "const Token&", StringOutMod_SpaceAfter,
					                argument.name);
					addStringOutput(output.source, convertedName, StringOutMod_SpaceAfter,
					                argument.name);
					addStringOutput(output.source, "= tokens[", StringOutMod_None, argument.name);
					OutputIndexName();
					addStringOutput(output.source, "]", StringOutMod_None, argument.name);
					addLangTokenOutput(output.source, StringOutMod_EndStatement, argument.name);
					break;
			}

#undef OutputIndexName
			++runtimeArgumentIndex;
			if (!isOptional)
				++numRequiredArguments;

			// Reset for next argument
			argument.name = nullptr;
			state = Name;
		}
	}

	if (state != Name)
	{
		ErrorAtToken(tokens[endArgsTokenIndex],
		             "expected type, or too many/too few arguments. Arguments are specified in "
		             "name type pairs");
		return false;
	}

	// TODO: Specifying &optional makes us have to check how many arguments at "runtime", then
	// expect that many (and no more)
	if (checkArgCount && numRequiredArguments && !isOptional)
	{
		addStringOutput(
		    output.source,
		    "if (!ExpectNumArguments(tokens, startTokenIndex, AutoArgs_endInvocationIndex,",
		    StringOutMod_None, &tokens[startArgsIndex]);
		addStringOutput(output.source, std::to_string(numRequiredArguments), StringOutMod_None,
		                &tokens[startArgsIndex]);
		addLangTokenOutput(output.source, StringOutMod_CloseParen, &tokens[startArgsIndex]);
		addLangTokenOutput(output.source, StringOutMod_CloseParen, &tokens[startArgsIndex]);
		addLangTokenOutput(output.source, StringOutMod_ScopeExitAll, &tokens[startArgsIndex]);
		addStringOutput(output.source, "return false", StringOutMod_SpaceBefore,
		                &tokens[startArgsIndex]);
		addLangTokenOutput(output.source, StringOutMod_EndStatement, &tokens[startArgsIndex]);
	}

	return true;
}

// TODO Consider merging with defgenerator?
bool DefMacroGenerator(EvaluatorEnvironment& environment, const EvaluatorContext& context,
                       const std::vector<Token>& tokens, int startTokenIndex,
                       GeneratorOutput& output)
{
	if (!ExpectEvaluatorScope("defmacro", tokens[startTokenIndex], context, EvaluatorScope_Module))
		return false;

	int endInvocationTokenIndex = FindCloseParenTokenIndex(tokens, startTokenIndex);
	int endTokenIndex = endInvocationTokenIndex;
	int startNameTokenIndex = startTokenIndex;
	StripInvocation(startNameTokenIndex, endTokenIndex);

	int nameIndex = startNameTokenIndex;
	const Token& nameToken = tokens[nameIndex];
	if (!ExpectTokenType("defmacro", nameToken, TokenType_Symbol))
		return false;

	if (findGenerator(environment, nameToken.contents.c_str()))
	{
		ErrorAtToken(nameToken,
		             "a generator by this name is defined. Generators always take precedence");
		return false;
	}

	int argsIndex = nameIndex + 1;
	if (!ExpectInInvocation("defmacro expected arguments", tokens, argsIndex,
	                        endInvocationTokenIndex))
		return false;
	const Token& argsStart = tokens[argsIndex];
	if (!ExpectTokenType("defmacro", argsStart, TokenType_OpenParen))
		return false;

	// Will be cleaned up when the environment is destroyed
	GeneratorOutput* compTimeOutput = new GeneratorOutput;

	ObjectDefinition newMacroDef = {};
	newMacroDef.definitionInvocation = &tokens[startTokenIndex];
	newMacroDef.name = nameToken.contents;
	newMacroDef.type = ObjectType_CompileTimeMacro;
	// Let the reference required propagation step handle this
	newMacroDef.isRequired = false;
	newMacroDef.context = context;
	newMacroDef.output = compTimeOutput;
	if (!addObjectDefinition(environment, newMacroDef))
	{
		delete compTimeOutput;
		return false;
	}

	// TODO: It would be nice to support global vs. local macros
	// This only really needs to be an environment distinction, not a code output distinction
	// Macros will be found without headers thanks to dynamic linking
	// bool isModuleLocal = tokens[startTokenIndex + 1].contents.compare("defmacro-local") == 0;

	if (environment.isMsvcCompiler)
		addStringOutput(compTimeOutput->source, "__declspec(dllexport)", StringOutMod_SpaceAfter,
		                &tokens[startTokenIndex]);

	// Macros must return success or failure
	addStringOutput(compTimeOutput->source, "bool", StringOutMod_SpaceAfter,
	                &tokens[startTokenIndex]);

	addStringOutput(compTimeOutput->source, nameToken.contents, StringOutMod_ConvertFunctionName,
	                &nameToken);

	addLangTokenOutput(compTimeOutput->source, StringOutMod_OpenParen, &argsStart);

	// Macros always receive the same arguments
	// TODO: Output macro arguments with proper output calls
	addStringOutput(compTimeOutput->source,
	                "EvaluatorEnvironment& environment, const EvaluatorContext& context, const "
	                "std::vector<Token>& tokens, int startTokenIndex, std::vector<Token>& output",
	                StringOutMod_None, &argsStart);

	int endArgsIndex = FindCloseParenTokenIndex(tokens, argsIndex);
	addLangTokenOutput(compTimeOutput->source, StringOutMod_CloseParen, &tokens[endArgsIndex]);

	int startBodyIndex = endArgsIndex + 1;
	addLangTokenOutput(compTimeOutput->source, StringOutMod_OpenScopeBlock,
	                   &tokens[startBodyIndex]);

	if (!ComptimeGenerateTokenArguments(tokens, argsIndex, *compTimeOutput))
		return false;

	// Evaluate our body!
	EvaluatorContext macroBodyContext = context;
	macroBodyContext.scope = EvaluatorScope_Body;
	// Let the reference required propagation step handle this
	macroBodyContext.isRequired = false;
	macroBodyContext.definitionName = &nameToken;
	macroBodyContext.delimiterTemplate = {};
	int numErrors = EvaluateGenerateAll_Recursive(environment, macroBodyContext, tokens,
	                                              startBodyIndex, *compTimeOutput);
	if (numErrors)
	{
		return false;
	}

	addLangTokenOutput(compTimeOutput->source, StringOutMod_CloseScopeBlock,
	                   &tokens[endTokenIndex]);

	return true;
}

// Essentially the same as DefMacro, though I could see them diverging or merging
bool DefGeneratorGenerator(EvaluatorEnvironment& environment, const EvaluatorContext& context,
                           const std::vector<Token>& tokens, int startTokenIndex,
                           GeneratorOutput& output)
{
	if (!ExpectEvaluatorScope("defgenerator", tokens[startTokenIndex], context,
	                          EvaluatorScope_Module))
		return false;

	int endInvocationTokenIndex = FindCloseParenTokenIndex(tokens, startTokenIndex);
	int endTokenIndex = endInvocationTokenIndex;
	int startNameTokenIndex = startTokenIndex;
	StripInvocation(startNameTokenIndex, endTokenIndex);

	int nameIndex = startNameTokenIndex;
	const Token& nameToken = tokens[nameIndex];
	if (!ExpectTokenType("defgenerator", nameToken, TokenType_Symbol))
		return false;

	int argsIndex = nameIndex + 1;
	if (!ExpectInInvocation("defgenerator expected arguments", tokens, argsIndex,
	                        endInvocationTokenIndex))
		return false;
	const Token& argsStart = tokens[argsIndex];
	if (!ExpectTokenType("defgenerator", argsStart, TokenType_OpenParen))
		return false;

	// Will be cleaned up when the environment is destroyed
	GeneratorOutput* compTimeOutput = new GeneratorOutput;

	ObjectDefinition newGeneratorDef = {};
	newGeneratorDef.definitionInvocation = &tokens[startTokenIndex];
	newGeneratorDef.name = nameToken.contents;
	newGeneratorDef.type = ObjectType_CompileTimeGenerator;
	// Let the reference required propagation step handle this
	newGeneratorDef.isRequired = false;
	newGeneratorDef.context = context;
	newGeneratorDef.output = compTimeOutput;
	if (!addObjectDefinition(environment, newGeneratorDef))
	{
		delete compTimeOutput;
		return false;
	}

	// TODO: It would be nice to support global vs. local generators
	// This only really needs to be an environment distinction, not a code output distinction
	// Generators will be found without headers thanks to dynamic linking
	// bool isModuleLocal = tokens[startTokenIndex + 1].contents.compare("defgenerator-local") == 0;

	if (environment.isMsvcCompiler)
		addStringOutput(compTimeOutput->source, "__declspec(dllexport)", StringOutMod_SpaceAfter,
		                &tokens[startTokenIndex]);

	// Generators must return success or failure
	addStringOutput(compTimeOutput->source, "bool", StringOutMod_SpaceAfter,
	                &tokens[startTokenIndex]);

	addStringOutput(compTimeOutput->source, nameToken.contents, StringOutMod_ConvertFunctionName,
	                &nameToken);

	addLangTokenOutput(compTimeOutput->source, StringOutMod_OpenParen, &argsStart);

	// Generators always receive the same arguments
	// TODO: Output generator arguments with proper output calls
	addStringOutput(compTimeOutput->source,
	                "EvaluatorEnvironment& environment, const EvaluatorContext& context, const "
	                "std::vector<Token>& tokens, int startTokenIndex, GeneratorOutput& output",
	                StringOutMod_None, &argsStart);

	int endArgsIndex = FindCloseParenTokenIndex(tokens, argsIndex);
	addLangTokenOutput(compTimeOutput->source, StringOutMod_CloseParen, &tokens[endArgsIndex]);

	int startBodyIndex = endArgsIndex + 1;
	addLangTokenOutput(compTimeOutput->source, StringOutMod_OpenScopeBlock,
	                   &tokens[startBodyIndex]);

	if (!ComptimeGenerateTokenArguments(tokens, argsIndex, *compTimeOutput))
		return false;

	// Evaluate our body!
	EvaluatorContext generatorBodyContext = context;
	generatorBodyContext.scope = EvaluatorScope_Body;
	// Let the reference required propagation step handle this
	generatorBodyContext.isRequired = false;
	generatorBodyContext.definitionName = &nameToken;
	generatorBodyContext.delimiterTemplate = {};
	int numErrors = EvaluateGenerateAll_Recursive(environment, generatorBodyContext, tokens,
	                                              startBodyIndex, *compTimeOutput);
	if (numErrors)
		return false;

	addLangTokenOutput(compTimeOutput->source, StringOutMod_CloseScopeBlock,
	                   &tokens[endTokenIndex]);

	return true;
}

bool DefStructGenerator(EvaluatorEnvironment& environment, const EvaluatorContext& context,
                        const std::vector<Token>& tokens, int startTokenIndex,
                        GeneratorOutput& output)
{
	if (IsForbiddenEvaluatorScope("defstruct", tokens[startTokenIndex], context,
	                              EvaluatorScope_ExpressionsOnly))
		return false;

	int endInvocationIndex = FindCloseParenTokenIndex(tokens, startTokenIndex);

	int nameIndex =
	    getExpectedArgument("expected struct name", tokens, startTokenIndex, 1, endInvocationIndex);
	if (nameIndex == -1 || !ExpectTokenType("defstruct", tokens[nameIndex], TokenType_Symbol))
		return false;

	// Structs defined in body scope are automatically local
	bool isGlobal = context.scope == EvaluatorScope_Module &&
	                tokens[startTokenIndex + 1].contents.compare("defstruct") == 0;

	std::vector<StringOutput>& outputDest = isGlobal ? output.header : output.source;

	addStringOutput(outputDest, "typedef struct", StringOutMod_SpaceAfter, &tokens[startTokenIndex]);

	addStringOutput(outputDest, tokens[nameIndex].contents, StringOutMod_ConvertTypeName,
	                &tokens[nameIndex]);
	addLangTokenOutput(outputDest, StringOutMod_OpenBlock, &tokens[nameIndex + 1]);

	struct
	{
		int nameIndex;
		int typeStart;
	} currentMember = {-1, -1};

	for (int i = nameIndex + 1; i < endInvocationIndex;
	     i = getNextArgument(tokens, i, endInvocationIndex))
	{
		if (currentMember.typeStart == -1 && currentMember.nameIndex != -1)
		{
			// Type
			currentMember.typeStart = i;
		}
		else
		{
			// Name
			if (!ExpectTokenType("defstruct member name", tokens[i], TokenType_Symbol))
				return false;

			currentMember.nameIndex = i;
		}

		if (currentMember.nameIndex != -1 && currentMember.typeStart != -1)
		{
			// Output finished member

			std::vector<StringOutput> typeOutput;
			std::vector<StringOutput> typeAfterNameOutput;
			// Arrays cannot be return types, they must be * instead
			if (!tokenizedCTypeToString_Recursive(
			        environment, context, tokens, currentMember.typeStart,
			        /*allowArray=*/true, typeOutput, typeAfterNameOutput))
				return false;

			// At this point, we probably have a valid variable. Start outputting
			addModifierToStringOutput(typeOutput.back(), StringOutMod_SpaceAfter);

			// Type
			PushBackAll(outputDest, typeOutput);

			// Name
			addStringOutput(outputDest, tokens[currentMember.nameIndex].contents,
			                StringOutMod_ConvertVariableName, &tokens[currentMember.nameIndex]);

			// Array
			PushBackAll(outputDest, typeAfterNameOutput);

			addLangTokenOutput(outputDest, StringOutMod_EndStatement,
			                   &tokens[currentMember.nameIndex]);

			// Prepare for next member
			currentMember.nameIndex = -1;
			currentMember.typeStart = -1;
		}
	}

	if (currentMember.nameIndex != -1 && currentMember.typeStart == -1)
	{
		ErrorAtToken(tokens[currentMember.nameIndex + 1], "expected type to follow member name");
		return false;
	}

	addLangTokenOutput(outputDest, StringOutMod_CloseBlock, &tokens[endInvocationIndex]);
	addStringOutput(outputDest, tokens[nameIndex].contents, StringOutMod_ConvertTypeName,
	                &tokens[nameIndex]);
	addLangTokenOutput(outputDest, StringOutMod_EndStatement, &tokens[endInvocationIndex]);

	return true;
}

bool IfGenerator(EvaluatorEnvironment& environment, const EvaluatorContext& context,
                 const std::vector<Token>& tokens, int startTokenIndex, GeneratorOutput& output)
{
	if (!ExpectEvaluatorScope("if", tokens[startTokenIndex], context, EvaluatorScope_Body))
		return false;

	int endInvocationIndex = FindCloseParenTokenIndex(tokens, startTokenIndex);

	int conditionIndex =
	    getExpectedArgument("expected condition", tokens, startTokenIndex, 1, endInvocationIndex);
	if (conditionIndex == -1)
		return false;

	int blockIndex =
	    getExpectedArgument("expected true block", tokens, startTokenIndex, 2, endInvocationIndex);
	if (blockIndex == -1)
		return false;

	addStringOutput(output.source, "if", StringOutMod_SpaceAfter, &tokens[startTokenIndex]);

	// Condition
	{
		addLangTokenOutput(output.source, StringOutMod_OpenParen, &tokens[conditionIndex]);
		EvaluatorContext expressionContext = context;
		expressionContext.scope = EvaluatorScope_ExpressionsOnly;
		if (EvaluateGenerate_Recursive(environment, expressionContext, tokens, conditionIndex,
		                               output) != 0)
			return false;
		addLangTokenOutput(output.source, StringOutMod_CloseParen, &tokens[blockIndex - 1]);
	}

	// True block
	{
		int scopedBlockIndex = blockAbsorbScope(tokens, blockIndex);

		addLangTokenOutput(output.source, StringOutMod_OpenScopeBlock, &tokens[scopedBlockIndex]);
		EvaluatorContext trueBlockBodyContext = context;
		trueBlockBodyContext.scope = EvaluatorScope_Body;
		trueBlockBodyContext.delimiterTemplate = {};
		int numErrors = 0;
		if (scopedBlockIndex != blockIndex)
			numErrors = EvaluateGenerateAll_Recursive(environment, trueBlockBodyContext, tokens,
			                                          scopedBlockIndex, output);
		else
			numErrors = EvaluateGenerate_Recursive(environment, trueBlockBodyContext, tokens,
			                                       scopedBlockIndex, output);
		if (numErrors)
			return false;
		addLangTokenOutput(output.source, StringOutMod_CloseScopeBlock, &tokens[scopedBlockIndex + 1]);
	}

	// Optional false block
	int falseBlockIndex = getNextArgument(tokens, blockIndex, endInvocationIndex);
	if (falseBlockIndex < endInvocationIndex)
	{
		int scopedFalseBlockIndex = blockAbsorbScope(tokens, falseBlockIndex);

		addStringOutput(output.source, "else", StringOutMod_None, &tokens[falseBlockIndex]);

		addLangTokenOutput(output.source, StringOutMod_OpenScopeBlock, &tokens[falseBlockIndex]);
		EvaluatorContext falseBlockBodyContext = context;
		falseBlockBodyContext.scope = EvaluatorScope_Body;
		falseBlockBodyContext.delimiterTemplate = {};
		int numErrors = 0;
		if (scopedFalseBlockIndex != falseBlockIndex)
			numErrors = EvaluateGenerateAll_Recursive(environment, falseBlockBodyContext, tokens,
			                                          scopedFalseBlockIndex, output);
		else
			numErrors = EvaluateGenerate_Recursive(environment, falseBlockBodyContext, tokens,
			                                       scopedFalseBlockIndex, output);
		if (numErrors)
			return false;
		addLangTokenOutput(output.source, StringOutMod_CloseScopeBlock,
		                   &tokens[falseBlockIndex + 1]);
	}

	int extraArgument = getNextArgument(tokens, falseBlockIndex, endInvocationIndex);
	if (extraArgument < endInvocationIndex)
	{
		ErrorAtToken(
		    tokens[extraArgument],
		    "if expects up to two blocks. If you want to have more than one statement in a block, "
		    "surround the statements in (scope), or use cond instead of if (etc.)");
		return false;
	}

	return true;
}

bool ConditionGenerator(EvaluatorEnvironment& environment, const EvaluatorContext& context,
                        const std::vector<Token>& tokens, int startTokenIndex,
                        GeneratorOutput& output)
{
	if (!ExpectEvaluatorScope("cond", tokens[startTokenIndex], context, EvaluatorScope_Body))
		return false;

	int endInvocationIndex = FindCloseParenTokenIndex(tokens, startTokenIndex);

	bool isFirstBlock = true;

	for (int currentConditionBlockIndex = startTokenIndex + 2;
	     currentConditionBlockIndex < endInvocationIndex;
	     currentConditionBlockIndex =
	         getNextArgument(tokens, currentConditionBlockIndex, endInvocationIndex))
	{
		if (!ExpectTokenType("cond", tokens[currentConditionBlockIndex], TokenType_OpenParen))
			return false;

		int endConditionBlockIndex = FindCloseParenTokenIndex(tokens, currentConditionBlockIndex);
		int conditionIndex = getExpectedArgument(
		    "expected condition", tokens, currentConditionBlockIndex, 0, endConditionBlockIndex);
		if (conditionIndex == -1)
			return false;

		if (!isFirstBlock)
			addStringOutput(output.source, "else", StringOutMod_SpaceAfter,
			                &tokens[conditionIndex]);

		// Special case: The "default" case of cond is conventionally marked with true as the
		// conditional. We'll support that, and not even write the if. If the cond is just (cond
		// (true blah)), make sure to still write the if (true)
		if (!isFirstBlock && tokens[conditionIndex].contents.compare("true") == 0)
		{
			if (endConditionBlockIndex + 1 != endInvocationIndex)
			{
				ErrorAtToken(tokens[conditionIndex],
				             "default case should be the last case in cond; otherwise, lower cases "
				             "will never be evaluated");
				return false;
			}
		}
		else
		{
			addStringOutput(output.source, "if", StringOutMod_SpaceAfter, &tokens[conditionIndex]);

			// Condition
			{
				addLangTokenOutput(output.source, StringOutMod_OpenParen, &tokens[conditionIndex]);
				EvaluatorContext expressionContext = context;
				expressionContext.scope = EvaluatorScope_ExpressionsOnly;
				if (EvaluateGenerate_Recursive(environment, expressionContext, tokens,
				                               conditionIndex, output) != 0)
					return false;
				addLangTokenOutput(output.source, StringOutMod_CloseParen, &tokens[conditionIndex]);
			}
		}

		// Don't require a block in the case that they want one condition to no-op and cancel
		// subsequent conditions
		int blockIndex = getArgument(tokens, currentConditionBlockIndex, 1, endConditionBlockIndex);
		if (blockIndex != -1)
		{
			addLangTokenOutput(output.source, StringOutMod_OpenScopeBlock, &tokens[blockIndex]);
			EvaluatorContext trueBlockBodyContext = context;
			trueBlockBodyContext.scope = EvaluatorScope_Body;
			trueBlockBodyContext.delimiterTemplate = {};
			int numErrors = EvaluateGenerateAll_Recursive(environment, trueBlockBodyContext, tokens,
			                                              blockIndex, output);
			if (numErrors)
				return false;
			addLangTokenOutput(output.source, StringOutMod_CloseScopeBlock,
			                   &tokens[blockIndex + 1]);
		}
		else
		{
			addLangTokenOutput(output.source, StringOutMod_OpenScopeBlock,
			                   &tokens[endConditionBlockIndex]);
			addLangTokenOutput(output.source, StringOutMod_CloseScopeBlock,
			                   &tokens[endConditionBlockIndex]);
		}

		isFirstBlock = false;
	}

	return true;
}

bool ObjectPathGenerator(EvaluatorEnvironment& environment, const EvaluatorContext& context,
                         const std::vector<Token>& tokens, int startTokenIndex,
                         GeneratorOutput& output)
{
	int endInvocationIndex = FindCloseParenTokenIndex(tokens, startTokenIndex);

	int startPathIndex = getExpectedArgument("expected object to path", tokens, startTokenIndex, 1,
	                                         endInvocationIndex);
	if (startPathIndex == -1)
		return false;

	enum
	{
		Object,
		PathType
	} state = Object;

	for (int i = startPathIndex; i < endInvocationIndex;
	     i = getNextArgument(tokens, i, endInvocationIndex))
	{
		if (state == PathType)
		{
			if (!ExpectTokenType("path type", tokens[i], TokenType_Symbol))
				return false;

			switch (tokens[i].contents[0])
			{
				case '>':
					addStringOutput(output.source, "->", StringOutMod_None, &tokens[i]);
					break;
				case '.':
					addStringOutput(output.source, ".", StringOutMod_None, &tokens[i]);
					break;
				case ':':
					addStringOutput(output.source, "::", StringOutMod_None, &tokens[i]);
					break;
				default:
					ErrorAtToken(tokens[i], "unknown path operation. Expected >, ., or :");
					return false;
			}

			state = Object;
		}
		else if (state == Object)
		{
			EvaluatorContext expressionContext = context;
			expressionContext.scope = EvaluatorScope_ExpressionsOnly;
			int numErrors =
			    EvaluateGenerate_Recursive(environment, expressionContext, tokens, i, output);
			if (numErrors)
				return false;

			state = PathType;
		}
	}

	return true;
}

static bool DefTypeAliasGenerator(EvaluatorEnvironment& environment,
                                  const EvaluatorContext& context, const std::vector<Token>& tokens,
                                  int startTokenIndex, GeneratorOutput& output)
{
	int endInvocationIndex = FindCloseParenTokenIndex(tokens, startTokenIndex);
	int nameIndex =
	    getExpectedArgument("name-index", tokens, startTokenIndex, 1, endInvocationIndex);
	if (-1 == nameIndex ||
	    !ExpectTokenType("def-type-alias alias", tokens[nameIndex], TokenType_Symbol))
		return false;

	int typeIndex =
	    getExpectedArgument("type-index", tokens, startTokenIndex, 2, endInvocationIndex);
	if (-1 == typeIndex)
		return false;

	const Token& invocationToken = tokens[1 + startTokenIndex];

	bool isGlobal = invocationToken.contents.compare("def-type-alias-global") == 0;

	std::vector<StringOutput> typeOutput;
	std::vector<StringOutput> typeAfterNameOutput;
	if (!(tokenizedCTypeToString_Recursive(environment, context, tokens, typeIndex, true,
	                                       typeOutput, typeAfterNameOutput)))
	{
		return false;
	}
	addModifierToStringOutput(typeOutput.back(), StringOutMod_SpaceAfter);

	std::vector<StringOutput>& outputDest = isGlobal ? output.header : output.source;

	NameStyleSettings nameStyle;
	char convertedName[MAX_NAME_LENGTH] = {0};
	lispNameStyleToCNameStyle(nameStyle.typeNameMode, tokens[nameIndex].contents.c_str(),
	                          convertedName, sizeof(convertedName), tokens[nameIndex]);

	addStringOutput(outputDest, "typedef", StringOutMod_SpaceAfter, &invocationToken);
	PushBackAll(outputDest, typeOutput);
	addStringOutput(outputDest, convertedName, StringOutMod_None, &tokens[nameIndex]);
	PushBackAll(outputDest, typeAfterNameOutput);
	addLangTokenOutput(outputDest, StringOutMod_EndStatement, &invocationToken);
	return true;
}

// Don't evaluate anything in me. Essentially a block comment
bool IgnoreGenerator(EvaluatorEnvironment& environment, const EvaluatorContext& context,
                     const std::vector<Token>& tokens, int startTokenIndex, GeneratorOutput& output)
{
	return true;
}

bool TokenizePushGenerator(EvaluatorEnvironment& environment, const EvaluatorContext& context,
                           const std::vector<Token>& tokens, int startTokenIndex,
                           GeneratorOutput& output)
{
	if (!ExpectEvaluatorScope("tokenize-push", tokens[startTokenIndex], context,
	                          EvaluatorScope_Body))
		return false;

	if (!context.definitionName)
	{
		ErrorAtToken(tokens[startTokenIndex],
		             "tokenize-push called in a context with no definition set. tokenize-push must "
		             "be invoked inside a definition");
		return false;
	}

	int endInvocationIndex = FindCloseParenTokenIndex(tokens, startTokenIndex);
	int outputIndex = getExpectedArgument("tokenize-push expected output variable name", tokens,
	                                      startTokenIndex, 1, endInvocationIndex);
	if (outputIndex == -1)
		return false;

	// Start off with a good token to refer back to in case of problems. In this case, use
	// "tokenize-push" which will tell the reader outputEvalHandle is created by the invocation
	// TODO: This token can't actually be referred to later. Rather than passing a token, take a
	// std::string instead?
	Token evaluateOutputTempVar = tokens[startTokenIndex + 1];
	MakeContextUniqueSymbolName(environment, context, "outputEvalHandle", &evaluateOutputTempVar);
	// Evaluate output variable
	{
		addStringOutput(output.source, "std::vector<Token>&", StringOutMod_SpaceAfter,
		                &tokens[startTokenIndex + 1]);
		addStringOutput(output.source, evaluateOutputTempVar.contents, StringOutMod_SpaceAfter,
		                &tokens[startTokenIndex + 1]);
		addStringOutput(output.source, "=", StringOutMod_SpaceAfter, &tokens[startTokenIndex + 1]);
		EvaluatorContext expressionContext = context;
		expressionContext.scope = EvaluatorScope_ExpressionsOnly;
		if (EvaluateGenerate_Recursive(environment, expressionContext, tokens, outputIndex,
		                               output) != 0)
			return false;
		addLangTokenOutput(output.source, StringOutMod_EndStatement, &tokens[startTokenIndex + 1]);
	}

	int startOutputToken = getExpectedArgument("tokenize-push expected tokens to output", tokens,
	                                           startTokenIndex, 2, endInvocationIndex);
	if (startOutputToken == -1)
		return false;

	addLangTokenOutput(output.source, StringOutMod_OpenBlock, &tokens[startTokenIndex]);
	addStringOutput(output.source, "TokenizePushContext spliceContext", StringOutMod_None,
	                &tokens[startTokenIndex]);
	addLangTokenOutput(output.source, StringOutMod_EndStatement, &tokens[startTokenIndex]);

	// Generate the CRC in order to retrieve the token list at macro runtime
	uint32_t tokensCrc = 0;

	for (int i = startOutputToken; i < endInvocationIndex; ++i)
	{
		const Token& currentToken = tokens[i];
		const Token& nextToken = tokens[i + 1];

		switch (currentToken.type)
		{
			case TokenType_OpenParen:
				crc32("(", 1, &tokensCrc);
				break;
			case TokenType_CloseParen:
				crc32(")", 1, &tokensCrc);
				break;
			case TokenType_Symbol:
				crc32(currentToken.contents.c_str(), currentToken.contents.size(), &tokensCrc);
				break;
			case TokenType_String:
				// It's unlikely there would be a collision without these, but we better be sure
				crc32("\"", 1, &tokensCrc);
				crc32(currentToken.contents.c_str(), currentToken.contents.size(), &tokensCrc);
				crc32("\"", 1, &tokensCrc);
				break;
			default:
				ErrorAtToken(currentToken, "token type not handled. This is likely a code error");
				return false;
		}

		// We only need to generate code when splices are referenced. Otherwise, the tokens are
		// pushed when executing the tokenize push
		if (currentToken.type == TokenType_OpenParen && nextToken.type == TokenType_Symbol &&
		    (nextToken.contents.compare("token-splice") == 0 ||
		     nextToken.contents.compare("token-splice-addr") == 0 ||
		     nextToken.contents.compare("token-splice-array") == 0 ||
		     nextToken.contents.compare("token-splice-rest") == 0))
		{
			// TODO: Performance: remove extra string compares
			bool isArray = nextToken.contents.compare("token-splice-array") == 0;
			bool isRest = nextToken.contents.compare("token-splice-rest") == 0;
			bool tokenMakePointer = isArray || nextToken.contents.compare("token-splice-addr") == 0;

			// Skip invocation
			int startSpliceArgs = i + 2;
			int endSpliceIndex = FindCloseParenTokenIndex(tokens, i);
			for (int spliceArg = startSpliceArgs; spliceArg < endSpliceIndex;
			     spliceArg = getNextArgument(tokens, spliceArg, endSpliceIndex))
			{
				if (isArray)
					addStringOutput(output.source, "TokenizePushSpliceArray(", StringOutMod_None,
					                &tokens[spliceArg]);
				else if (isRest)
					addStringOutput(output.source, "TokenizePushSpliceAllTokenExpressions(",
					                StringOutMod_None, &tokens[spliceArg]);
				else
					addStringOutput(output.source, "TokenizePushSpliceTokenExpression(",
					                StringOutMod_None, &tokens[spliceArg]);

				addStringOutput(output.source, "&spliceContext", StringOutMod_None,
				                &tokens[spliceArg]);
				addLangTokenOutput(output.source, StringOutMod_ListSeparator, &tokens[spliceArg]);

				if (tokenMakePointer)
					addStringOutput(output.source, "&(", StringOutMod_None, &tokens[spliceArg]);

				// Evaluate token to start output expression
				EvaluatorContext expressionContext = context;
				expressionContext.scope = EvaluatorScope_ExpressionsOnly;
				if (EvaluateGenerate_Recursive(environment, expressionContext, tokens, spliceArg,
				                               output) != 0)
					return false;

				if (tokenMakePointer)
					addLangTokenOutput(output.source, StringOutMod_CloseParen, &tokens[spliceArg]);

				// Second argument is tokens array. Need to get it for bounds check
				bool shouldBreak = false;
				if (isRest)
				{
					int tokenArrayArg = getExpectedArgument(
					    "token-splice-rest requires the array which holds the token to splice as "
					    "the second argument, e.g. (token-splice-rest my-start-token tokens), "
					    "where my-start-token is a pointer to a token stored within 'tokens'",
					    tokens, i, 2, endSpliceIndex);
					if (tokenArrayArg == -1)
						return false;

					addLangTokenOutput(output.source, StringOutMod_ListSeparator,
					                   &tokens[spliceArg]);

					addStringOutput(output.source, "&(", StringOutMod_None, &tokens[spliceArg]);
					EvaluatorContext expressionContext = context;
					expressionContext.scope = EvaluatorScope_ExpressionsOnly;
					if (EvaluateGenerate_Recursive(environment, expressionContext, tokens,
					                               tokenArrayArg, output) != 0)
						return false;

					addLangTokenOutput(output.source, StringOutMod_CloseParen, &tokens[spliceArg]);
					shouldBreak = true;
				}

				addLangTokenOutput(output.source, StringOutMod_CloseParen, &tokens[spliceArg]);
				addLangTokenOutput(output.source, StringOutMod_EndStatement, &tokens[spliceArg]);

				// All splices accept multiple args except token-splice-rest, because it needs two
				// args, and things would get confusing
				if (shouldBreak)
					break;
			}

			// Finished splice list
			i = endSpliceIndex;
		}
	}

	// Add the tokens for later retrieval
	{
		// TODO: Detect collisions via comparing tokens if pointer is different and tokens are
		// different TokenizePushTokensMap::iterator findIt =
		// definition.tokenizePushTokens.find(tokensCrc);
		// if (findIt != definition.tokenizePushTokens.end())
		// {
		// if (findIt.second != &tokens[startOutputToken])
		// {
		// 	ErrorAtToken(tokens[startOutputToken], "collision detected between two different token
		// "); 	return false;
		// }
		// }
		ObjectDefinition* definition =
		    findObjectDefinition(environment, context.definitionName->contents.c_str());
		if (!definition)
		{
			ErrorAtTokenf(tokens[startTokenIndex],
			              "could not find definition %s despite definition being set. Code error?",
			              context.definitionName->contents.c_str());
			return false;
		}
		definition->tokenizePushTokens[tokensCrc] = &tokens[startOutputToken];
	}

	addStringOutput(output.source, "if (!TokenizePushExecute(", StringOutMod_None,
	                &tokens[startTokenIndex]);

	addStringOutput(output.source, "environment", StringOutMod_None, &tokens[startTokenIndex]);
	addLangTokenOutput(output.source, StringOutMod_ListSeparator, &tokens[startTokenIndex]);

	addStringOutput(output.source, context.definitionName->contents.c_str(),
	                StringOutMod_SurroundWithQuotes, &tokens[startTokenIndex]);
	addLangTokenOutput(output.source, StringOutMod_ListSeparator, &tokens[startTokenIndex]);

	// Retrieve the tokens from memory
	addStringOutput(output.source, std::to_string(tokensCrc).c_str(), StringOutMod_None,
	                &tokens[startTokenIndex]);
	addLangTokenOutput(output.source, StringOutMod_ListSeparator, &tokens[startTokenIndex]);

	// Use these arguments to splice tokens in
	addStringOutput(output.source, "&spliceContext", StringOutMod_None, &tokens[startTokenIndex]);
	addLangTokenOutput(output.source, StringOutMod_ListSeparator, &tokens[startTokenIndex]);

	// Write output argument
	addStringOutput(output.source, evaluateOutputTempVar.contents, StringOutMod_None,
	                &tokens[startTokenIndex]);
	// addLangTokenOutput(output.source, StringOutMod_ListSeparator, &tokens[startTokenIndex]);

	// Yikes
	addLangTokenOutput(output.source, StringOutMod_CloseParen, &tokens[startTokenIndex]);
	addLangTokenOutput(output.source, StringOutMod_CloseParen, &tokens[startTokenIndex]);
	addLangTokenOutput(output.source, StringOutMod_OpenBlock, &tokens[startTokenIndex]);
	addLangTokenOutput(output.source, StringOutMod_ScopeExitAll, &tokens[startTokenIndex]);
	addStringOutput(output.source, "return false", StringOutMod_None, &tokens[startTokenIndex]);
	addLangTokenOutput(output.source, StringOutMod_EndStatement, &tokens[startTokenIndex]);
	addLangTokenOutput(output.source, StringOutMod_CloseBlock, &tokens[startTokenIndex]);
	addLangTokenOutput(output.source, StringOutMod_CloseBlock, &tokens[startTokenIndex]);
	return true;
}

//
// Compile-time conditional compilation (similar to C preprocessor #if)
//
bool ComptimeErrorGenerator(EvaluatorEnvironment& environment, const EvaluatorContext& context,
                            const std::vector<Token>& tokens, int startTokenIndex,
                            GeneratorOutput& output)
{
	int endInvocationIndex = FindCloseParenTokenIndex(tokens, startTokenIndex);
	int messageIndex = getExpectedArgument("comptime-error expected message", tokens,
	                                       startTokenIndex, 1, endInvocationIndex);
	if (messageIndex == -1 ||
	    !ExpectTokenType("comptime-error message", tokens[messageIndex], TokenType_String))
		return false;

	ErrorAtTokenf(tokens[startTokenIndex], "%s", tokens[messageIndex].contents.c_str());
	return false;
}

bool ComptimeCondGenerator(EvaluatorEnvironment& environment, const EvaluatorContext& context,
                           const std::vector<Token>& tokens, int startTokenIndex,
                           GeneratorOutput& output)
{
	int endInvocationIndex = FindCloseParenTokenIndex(tokens, startTokenIndex);

	bool isFirstBlock = true;

	for (int currentConditionBlockIndex = startTokenIndex + 2;
	     currentConditionBlockIndex < endInvocationIndex;
	     currentConditionBlockIndex =
	         getNextArgument(tokens, currentConditionBlockIndex, endInvocationIndex))
	{
		if (!ExpectTokenType("cond", tokens[currentConditionBlockIndex], TokenType_OpenParen))
			return false;

		int endConditionBlockIndex = FindCloseParenTokenIndex(tokens, currentConditionBlockIndex);
		int conditionIndex = getExpectedArgument(
		    "expected condition", tokens, currentConditionBlockIndex, 0, endConditionBlockIndex);
		if (conditionIndex == -1)
			return false;

		bool isExplicitTrue = tokens[conditionIndex].contents.compare("true") == 0;

		// Special case: The "default" case of cond is conventionally marked with true as the
		// conditional. We'll support that, and not even write the if. If the cond is just (cond
		// (true blah)), make sure to still write the if (true)
		if (!isFirstBlock && isExplicitTrue)
		{
			if (endConditionBlockIndex + 1 != endInvocationIndex)
			{
				ErrorAtToken(tokens[conditionIndex],
				             "default case should be the last case in cond; otherwise, lower cases "
				             "will never be evaluated");
				return false;
			}
		}

		bool conditionResult = isExplicitTrue;
		// Only evaluate the condition if it isn't already "true"
		if (!isExplicitTrue)
		{
			if (!CompileTimeEvaluateCondition(environment, context, tokens, conditionIndex,
			                                  conditionResult))
			{
				// Evaluation itself failed
				return false;
			}
		}

		if (conditionResult)
		{
			int blockIndex =
			    getArgument(tokens, currentConditionBlockIndex, 1, endConditionBlockIndex);
			if (blockIndex != -1)
			{
				EvaluatorContext trueBlockBodyContext = context;
				int numErrors = EvaluateGenerateAll_Recursive(environment, trueBlockBodyContext,
				                                              tokens, blockIndex, output);
				if (numErrors)
					return false;
			}

			// Found the first true condition
			break;
		}

		isFirstBlock = false;
	}

	return true;
}

bool ComptimeDefineSymbolGenerator(EvaluatorEnvironment& environment,
                                   const EvaluatorContext& context,
                                   const std::vector<Token>& tokens, int startTokenIndex,
                                   GeneratorOutput& output)
{
	int endInvocationIndex = FindCloseParenTokenIndex(tokens, startTokenIndex);
	int symbolIndex = getExpectedArgument("comptime-define-symbol expected symbol", tokens,
	                                      startTokenIndex, 1, endInvocationIndex);
	if (symbolIndex == -1 ||
	    !ExpectTokenType("comptime-define-symbol message", tokens[symbolIndex], TokenType_Symbol))
		return false;

	if (!isSpecialSymbol(tokens[symbolIndex]))
	{
		ErrorAtToken(tokens[symbolIndex], "symbols are defined with prefixed ', e.g. 'Unix");
		return false;
	}

	environment.compileTimeSymbols[tokens[symbolIndex].contents] = true;

	return true;
}

// Export works like a delayed evaluate where it is evaluated within each importing module
bool ExportScopeGenerator(EvaluatorEnvironment& environment, const EvaluatorContext& context,
                          const std::vector<Token>& tokens, int startTokenIndex,
                          GeneratorOutput& output)
{
	// Don't let the user think this function can be called during comptime/runtime
	if (!ExpectEvaluatorScope("export", tokens[startTokenIndex], context, EvaluatorScope_Module))
		return false;

	if (!context.module)
	{
		ErrorAtToken(tokens[startTokenIndex], "modules not supported (internal code error?)");
		return false;
	}

	// See this member's comment to understand why this is here
	if (context.module->exportScopesLocked)
	{
		ErrorAtToken(
		    tokens[startTokenIndex],
		    "export has been added, but other modules have already evaluated past exports of this "
		    "module. Try to move this export out of comptime blocks or do not macro-generate it");
		return false;
	}

	const Token& startEvalToken = tokens[startTokenIndex + EXPORT_SCOPE_START_EVAL_OFFSET];
	if (startEvalToken.type == TokenType_CloseParen)
	{
		ErrorAtToken(startEvalToken, "expected statements to export");
		return false;
	}

	ModuleExportScope newExport;
	newExport.tokens = &tokens;
	newExport.startTokenIndex = startTokenIndex;
	context.module->exportScopes.push_back(newExport);

	// We also want to evaluate the scope in the current module
	if (tokens[startTokenIndex + 1].contents.compare("export-and-evaluate") == 0)
	{
		EvaluatorContext exportEvaluateContext = context;
		int numErrors =
		    EvaluateGenerateAll_Recursive(environment, exportEvaluateContext, tokens,
		                                  startTokenIndex + EXPORT_SCOPE_START_EVAL_OFFSET, output);
		if (numErrors)
			return false;
	}

	return true;
}

bool SplicePointGenerator(EvaluatorEnvironment& environment, const EvaluatorContext& context,
                          const std::vector<Token>& tokens, int startTokenIndex,
                          GeneratorOutput& output)
{
	// Don't let the user think this function can be called during comptime/runtime
	if (!ExpectEvaluatorScope("splice-point", tokens[startTokenIndex], context,
	                          EvaluatorScope_Module))
		return false;

	int endInvocationIndex = FindCloseParenTokenIndex(tokens, startTokenIndex);
	int spliceNameIndex = getExpectedArgument("splice-point expected name of splice point", tokens,
											  startTokenIndex, 1, endInvocationIndex);
	if (spliceNameIndex == -1 ||
	    !ExpectTokenType("splice-point name", tokens[spliceNameIndex], TokenType_Symbol))
		return false;

	const Token* spliceName = &tokens[spliceNameIndex];

	SplicePointTableIterator findIt = environment.splicePoints.find(spliceName->contents);
	if (findIt != environment.splicePoints.end())
	{
		ErrorAtToken(*spliceName,
		             "splice point redefined. Splice points must have unique names, and may only "
		             "be evaluated once");
		return false;
	}

	GeneratorOutput* spliceOutput = new GeneratorOutput;
	addSpliceOutput(output, spliceOutput, &tokens[startTokenIndex]);

	SplicePoint newSplicePoint;
	newSplicePoint.output = spliceOutput;
	newSplicePoint.context = context;
	newSplicePoint.blameToken = &tokens[startTokenIndex];

	environment.splicePoints[spliceName->contents] = newSplicePoint;

	return true;
}

bool DeferGenerator(EvaluatorEnvironment& environment, const EvaluatorContext& context,
					const std::vector<Token>& tokens, int startTokenIndex,
					GeneratorOutput& output)
{
	if (!ExpectEvaluatorScope("defer", tokens[startTokenIndex], context,
	                          EvaluatorScope_Body))
		return false;

	GeneratorOutput* spliceOutput = new GeneratorOutput;
	// Make sure the splice gets cleaned up once everything's done
	environment.orphanedOutputs.push_back(spliceOutput);
	addSpliceOutputWithModifiers(output, spliceOutput, &tokens[startTokenIndex],
	                             StringOutMod_SpliceOnScopeExit);

	EvaluatorContext deferContext = context;
	int numErrors = EvaluateGenerateAll_Recursive(environment, deferContext, tokens,
	                                              startTokenIndex + 2, *spliceOutput);
	if (numErrors)
		return false;

	return true;
}

// Give the user a replacement suggestion
typedef std::unordered_map<std::string, const char*> DeprecatedHelpStringMap;
DeprecatedHelpStringMap s_deprecatedHelpStrings;
bool DeprecatedGenerator(EvaluatorEnvironment& environment, const EvaluatorContext& context,
                         const std::vector<Token>& tokens, int startTokenIndex,
                         GeneratorOutput& output)
{
	DeprecatedHelpStringMap::iterator findIt =
	    s_deprecatedHelpStrings.find(tokens[startTokenIndex + 1].contents.c_str());
	if (findIt != s_deprecatedHelpStrings.end())
		ErrorAtTokenf(tokens[startTokenIndex], "this function is now deprecated. %s",
		             findIt->second);
	else
		ErrorAtToken(tokens[startTokenIndex], "this function is now deprecated");
	return false;
}

//
// CStatements
//

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
	    {CloseParen, nullptr, -1}, {OpenContinueBreakableScope, nullptr, -1}, {Body, nullptr, 2},
	    {CloseContinueBreakableScope, nullptr, -1}};

	const CStatementOperation rangeBasedFor[] = {
	    {Keyword, "for", -1},     {OpenParen, nullptr, -1},  {TypeNoArray, nullptr, 2},
	    {Keyword, " ", -1},       {Expression, nullptr, 1},  {Keyword, ":", -1},
	    {Expression, nullptr, 3}, {CloseParen, nullptr, -1}, {OpenContinueBreakableScope, nullptr, -1},
	    {Body, nullptr, 4},       {CloseContinueBreakableScope, nullptr, -1}};

	// Conditionals
	const CStatementOperation whenStatement[] = {
	    {Keyword, "if", -1},       {OpenParen, nullptr, -1}, {Expression, nullptr, 1},
	    {CloseParen, nullptr, -1}, {OpenScope, nullptr, -1}, {Body, nullptr, 2},
	    {CloseScope, nullptr, -1}};
	const CStatementOperation unlessStatement[] = {
	    {Keyword, "if", -1},       {OpenParen, nullptr, -1}, {KeywordNoSpace, "!", -1},
	    {OpenParen, nullptr, -1},  {Expression, nullptr, 1}, {CloseParen, nullptr, -1},
	    {CloseParen, nullptr, -1}, {OpenScope, nullptr, -1}, {Body, nullptr, 2},
	    {CloseScope, nullptr, -1}};

	const CStatementOperation ternaryOperatorStatement[] = {
	    {OpenParen, nullptr, -1},  {Expression /*Name*/, nullptr, 1},
	    {Keyword, "?", -1},        {Expression, nullptr, 2},
	    {Keyword, ":", -1},        {Expression, nullptr, 3},
	    {CloseParen, nullptr, -1}, {SmartEndStatement, nullptr, -1}};

	// Control flow
	const CStatementOperation returnStatement[] = {{ExitAllScopes, nullptr, -1},
	                                               {Keyword, "return", -1},
	                                               {ExpressionOptional, nullptr, 1},
	                                               {SmartEndStatement, nullptr, -1}};

	const CStatementOperation continueStatement[] = {{ContinueOrBreakInScope, nullptr, -1},
	                                                 {KeywordNoSpace, "continue", -1},
	                                                 {SmartEndStatement, nullptr, -1}};

	const CStatementOperation breakStatement[] = {{ContinueOrBreakInScope, nullptr, -1},
	                                              {KeywordNoSpace, "break", -1},
	                                              {SmartEndStatement, nullptr, -1}};

	const CStatementOperation newStatement[] = {
	    {Keyword, "new", -1}, {TypeNoArray, nullptr, 1}, {SmartEndStatement, nullptr, -1}};
	const CStatementOperation deleteStatement[] = {
	    {Keyword, "delete", -1}, {Expression, nullptr, 1}, {SmartEndStatement, nullptr, -1}};
	const CStatementOperation newArrayStatement[] = {
	    {Keyword, "new", -1},     {TypeNoArray, nullptr, 2}, {KeywordNoSpace, "[", -1},
	    {Expression, nullptr, 1}, {KeywordNoSpace, "]", -1}, {SmartEndStatement, nullptr, -1}};
	const CStatementOperation deleteArrayStatement[] = {
	    {Keyword, "delete[]", -1}, {Expression, nullptr, 1}, {SmartEndStatement, nullptr, -1}};

	const CStatementOperation initializerList[] = {
	    {OpenList, nullptr, -1}, {ExpressionList, nullptr, 1}, {CloseList, nullptr, -1}};

	const CStatementOperation assignmentStatement[] = {{Expression /*Name*/, nullptr, 1},
	                                                   {Keyword, "=", -1},
	                                                   {Expression, nullptr, 2},
	                                                   {SmartEndStatement, nullptr, -1}};

	const CStatementOperation dereference[] = {{OpenParen, nullptr, -1},
	                                           {KeywordNoSpace, "*", -1},
	                                           {Expression, nullptr, 1},
	                                           {CloseParen, nullptr, -1}};
	const CStatementOperation addressOf[] = {{OpenParen, nullptr, -1},
	                                         {KeywordNoSpace, "&", -1},
	                                         {Expression, nullptr, 1},
	                                         {CloseParen, nullptr, -1}};

	const CStatementOperation field[] = {{SpliceNoSpace, ".", 1}};

	const CStatementOperation memberFunctionInvocation[] = {
	    {Expression, nullptr, 2},        {KeywordNoSpace, ".", -1},    {Expression, nullptr, 1},
	    {OpenParen, nullptr, -1},        {ExpressionList, nullptr, 3}, {CloseParen, nullptr, -1},
	    {SmartEndStatement, nullptr, -1}};

	const CStatementOperation dereferenceMemberFunctionInvocation[] = {
	    {Expression, nullptr, 2},        {KeywordNoSpace, "->", -1},   {Expression, nullptr, 1},
	    {OpenParen, nullptr, -1},        {ExpressionList, nullptr, 3}, {CloseParen, nullptr, -1},
	    {SmartEndStatement, nullptr, -1}};

	// Useful in the case of calling functions in namespaces. Shouldn't be used otherwise
	const CStatementOperation callFunctionInvocation[] = {{Expression, nullptr, 1},
	                                                      {OpenParen, nullptr, -1},
	                                                      {ExpressionList, nullptr, 2},
	                                                      {CloseParen, nullptr, -1},
	                                                      {SmartEndStatement, nullptr, -1}};

	const CStatementOperation castStatement[] = {{OpenParen, nullptr, -1},
	                                             {OpenParen, nullptr, -1},
	                                             {TypeNoArray, nullptr, 2},
	                                             {CloseParen, nullptr, -1},
	                                             {Expression /*Thing to cast*/, nullptr, 1},
	                                             {CloseParen, nullptr, -1}};

	// Necessary to parse types correctly, because it's a DSL
	const CStatementOperation typeStatement[] = {{TypeNoArray, nullptr, 1}};

	const CStatementOperation scopeResolution[] = {{SpliceNoSpace, "::", 1}};

	// Similar to progn, but doesn't necessarily mean things run in order (this doesn't add
	// barriers or anything). It's useful both for making arbitrary scopes and for making if
	// blocks with multiple statements
	const CStatementOperation blockStatement[] = {
	    {OpenScope, nullptr, -1}, {Body, nullptr, 1}, {CloseScope, nullptr, -1}};

	// https://www.tutorialspoint.com/cprogramming/c_operators.htm proved useful
	// These could probably be made smarter to not need all the redundant parentheses. For now I'll
	// make it absolutely unambiguous
	const CStatementOperation booleanOr[] = {
	    {OpenParen, nullptr, -1},
	    {Splice, "||", 1},
	    {CloseParen, nullptr, -1},
	};
	const CStatementOperation booleanAnd[] = {
	    {OpenParen, nullptr, -1},
	    {Splice, "&&", 1},
	    {CloseParen, nullptr, -1},
	};
	const CStatementOperation booleanNot[] = {{KeywordNoSpace, "!", -1},
	                                          {OpenParen, nullptr, -1},
	                                          {Expression, nullptr, 1},
	                                          {CloseParen, nullptr, -1}};

	const CStatementOperation bitwiseOr[] = {
	    {OpenParen, nullptr, -1}, {Splice, "|", 1}, {CloseParen, nullptr, -1}};
	const CStatementOperation bitwiseAnd[] = {
	    {OpenParen, nullptr, -1}, {Splice, "&", 1}, {CloseParen, nullptr, -1}};
	const CStatementOperation bitwiseXOr[] = {
	    {OpenParen, nullptr, -1}, {Splice, "^", 1}, {CloseParen, nullptr, -1}};
	const CStatementOperation bitwiseOnesComplement[] = {{Keyword, "~", -1},
	                                                     {Expression, nullptr, 1}};
	const CStatementOperation bitwiseLeftShift[] = {{Splice, "<<", 1}};
	const CStatementOperation bitwiseRightShift[] = {{Splice, ">>", 1}};

	// Need both parens and only accept two parameters, otherwise operator precedence will confuse
	const CStatementOperation relationalEquality[] = {{OpenParen, nullptr, -1},
	                                                  {Expression, nullptr, 1},
	                                                  {Keyword, "==", -1},
	                                                  {Expression, nullptr, 2},
	                                                  {CloseParen, nullptr, -1}};
	const CStatementOperation relationalNotEqual[] = {{OpenParen, nullptr, -1},
	                                                  {Expression, nullptr, 1},
	                                                  {Keyword, "!=", -1},
	                                                  {Expression, nullptr, 2},
	                                                  {CloseParen, nullptr, -1}};
	const CStatementOperation relationalLessThanEqual[] = {{OpenParen, nullptr, -1},
	                                                       {Expression, nullptr, 1},
	                                                       {Keyword, "<=", -1},
	                                                       {Expression, nullptr, 2},
	                                                       {CloseParen, nullptr, -1}};
	const CStatementOperation relationalGreaterThanEqual[] = {{OpenParen, nullptr, -1},
	                                                          {Expression, nullptr, 1},
	                                                          {Keyword, ">=", -1},
	                                                          {Expression, nullptr, 2},
	                                                          {CloseParen, nullptr, -1}};
	const CStatementOperation relationalLessThan[] = {{OpenParen, nullptr, -1},
	                                                  {Expression, nullptr, 1},
	                                                  {Keyword, "<", -1},
	                                                  {Expression, nullptr, 2},
	                                                  {CloseParen, nullptr, -1}};
	const CStatementOperation relationalGreaterThan[] = {{OpenParen, nullptr, -1},
	                                                     {Expression, nullptr, 1},
	                                                     {Keyword, ">", -1},
	                                                     {Expression, nullptr, 2},
	                                                     {CloseParen, nullptr, -1}};

	// Parentheses are especially necessary because the user's expectation will be broken without
	// For example: (/ (- a b) c)
	// Without parentheses, that outputs a - b / c, which due to operator precedence, is a - (b/c)
	// With parentheses, we get the correct ((a - b) / c)
	const CStatementOperation add[] = {
	    {OpenParen, nullptr, -1}, {Splice, "+", 1}, {CloseParen, nullptr, -1}};
	const CStatementOperation subtract[] = {
	    {OpenParen, nullptr, -1}, {Splice, "-", 1}, {CloseParen, nullptr, -1}};
	const CStatementOperation multiply[] = {
	    {OpenParen, nullptr, -1}, {Splice, "*", 1}, {CloseParen, nullptr, -1}};
	const CStatementOperation divide[] = {
	    {OpenParen, nullptr, -1}, {Splice, "/", 1}, {CloseParen, nullptr, -1}};
	const CStatementOperation modulus[] = {
	    {OpenParen, nullptr, -1}, {Splice, "%", 1}, {CloseParen, nullptr, -1}};
	// Always pre-increment, which matches what you'd expect given the invocation comes before
	// the expression. It's also slightly faster, yadda yadda
	const CStatementOperation increment[] = {
	    {KeywordNoSpace, "++", -1}, {Expression, nullptr, 1}, {SmartEndStatement, nullptr, -1}};
	const CStatementOperation decrement[] = {
	    {KeywordNoSpace, "--", -1}, {Expression, nullptr, 1}, {SmartEndStatement, nullptr, -1}};

	// Useful for marking e.g. the increment statement in a for loop blank
	// const CStatementOperation noOpStatement[] = {};

	const struct
	{
		const char* name;
		const CStatementOperation* operations;
		int numOperations;
		RequiredFeature requiresFeature;
	} statementOperators[] = {
	    {"while", whileStatement, ArraySize(whileStatement), RequiredFeature_None},
	    {"for-in", rangeBasedFor, ArraySize(rangeBasedFor), RequiredFeature_CppInDefinition},
	    {"return", returnStatement, ArraySize(returnStatement), RequiredFeature_None},
	    {"continue", continueStatement, ArraySize(continueStatement), RequiredFeature_None},
	    {"break", breakStatement, ArraySize(breakStatement), RequiredFeature_None},
	    {"when", whenStatement, ArraySize(whenStatement), RequiredFeature_None},
	    {"unless", unlessStatement, ArraySize(unlessStatement), RequiredFeature_None},
	    {"array", initializerList, ArraySize(initializerList), RequiredFeature_None},
	    {"set", assignmentStatement, ArraySize(assignmentStatement), RequiredFeature_None},
	    // Alias of block, in case you want to be explicit. For example, creating a scope to reduce
	    // scope of variables vs. creating a block to have more than one statement in an (if) body
	    {"scope", blockStatement, ArraySize(blockStatement), RequiredFeature_None},
	    {"block", blockStatement, ArraySize(blockStatement), RequiredFeature_None},
	    {"?", ternaryOperatorStatement, ArraySize(ternaryOperatorStatement), RequiredFeature_None},
	    {"new", newStatement, ArraySize(newStatement), RequiredFeature_CppInDefinition},
	    {"delete", deleteStatement, ArraySize(deleteStatement), RequiredFeature_CppInDefinition},
	    {"new-array", newArrayStatement, ArraySize(newArrayStatement), RequiredFeature_CppInDefinition},
	    {"delete-array", deleteArrayStatement, ArraySize(deleteArrayStatement), RequiredFeature_CppInDefinition},
	    // Pointers
	    {"deref", dereference, ArraySize(dereference), RequiredFeature_None},
	    {"addr", addressOf, ArraySize(addressOf), RequiredFeature_None},
	    {"field", field, ArraySize(field), RequiredFeature_None},
	    {"call-on", memberFunctionInvocation, ArraySize(memberFunctionInvocation), RequiredFeature_None},
	    {"call-on-ptr", dereferenceMemberFunctionInvocation,
	     ArraySize(dereferenceMemberFunctionInvocation), RequiredFeature_CppInDefinition},
	    {"call", callFunctionInvocation, ArraySize(callFunctionInvocation), RequiredFeature_None},
		// TODO: Determine whether Cpp is required in header or not
	    {"in", scopeResolution, ArraySize(scopeResolution), RequiredFeature_Cpp},
	    {"type-cast", castStatement, ArraySize(castStatement), RequiredFeature_None},
	    {"type", typeStatement, ArraySize(typeStatement), RequiredFeature_None},
	    // Expressions
	    {"or", booleanOr, ArraySize(booleanOr), RequiredFeature_None},
	    {"and", booleanAnd, ArraySize(booleanAnd), RequiredFeature_None},
	    {"not", booleanNot, ArraySize(booleanNot), RequiredFeature_None},
	    {"bit-or", bitwiseOr, ArraySize(bitwiseOr), RequiredFeature_None},
	    {"bit-and", bitwiseAnd, ArraySize(bitwiseAnd), RequiredFeature_None},
	    {"bit-xor", bitwiseXOr, ArraySize(bitwiseXOr), RequiredFeature_None},
	    {"bit-ones-complement", bitwiseOnesComplement, ArraySize(bitwiseOnesComplement), RequiredFeature_None},
	    {"bit-<<", bitwiseLeftShift, ArraySize(bitwiseLeftShift), RequiredFeature_None},
	    {"bit->>", bitwiseRightShift, ArraySize(bitwiseRightShift), RequiredFeature_None},
	    {"=", relationalEquality, ArraySize(relationalEquality), RequiredFeature_None},
	    {"!=", relationalNotEqual, ArraySize(relationalNotEqual), RequiredFeature_None},
	    // {"eq", relationalEquality, ArraySize(relationalEquality), RequiredFeature_None},
	    // {"neq", relationalNotEqual, ArraySize(relationalNotEqual), RequiredFeature_None},
	    {"<=", relationalLessThanEqual, ArraySize(relationalLessThanEqual), RequiredFeature_None},
	    {">=", relationalGreaterThanEqual, ArraySize(relationalGreaterThanEqual), RequiredFeature_None},
	    {"<", relationalLessThan, ArraySize(relationalLessThan), RequiredFeature_None},
	    {">", relationalGreaterThan, ArraySize(relationalGreaterThan), RequiredFeature_None},
	    // Arithmetic
	    {"+", add, ArraySize(add), RequiredFeature_None},
	    {"-", subtract, ArraySize(subtract), RequiredFeature_None},
	    {"*", multiply, ArraySize(multiply), RequiredFeature_None},
	    {"/", divide, ArraySize(divide), RequiredFeature_None},
	    {"%", modulus, ArraySize(modulus), RequiredFeature_None},
	    {"mod", modulus, ArraySize(modulus), RequiredFeature_None},
	    {"++", increment, ArraySize(increment), RequiredFeature_None},
	    {"--", decrement, ArraySize(decrement), RequiredFeature_None},
	    {"incr", increment, ArraySize(increment), RequiredFeature_None},
	    {"decr", decrement, ArraySize(decrement), RequiredFeature_None},
	};

	for (unsigned int i = 0; i < ArraySize(statementOperators); ++i)
	{
		if (nameToken.contents.compare(statementOperators[i].name) == 0)
		{
			if (statementOperators[i].requiresFeature != RequiredFeature_None)
				RequiresFeature(
				    context.module,
				    findObjectDefinition(environment, context.definitionName->contents.c_str()),
				    statementOperators[i].requiresFeature, &nameToken);

			return CStatementOutput(environment, context, tokens, startTokenIndex,
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

// An example of a macro in C++
#if 0
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
#endif

//
// Environment interaction
//
void importFundamentalGenerators(EvaluatorEnvironment& environment)
{
	environment.generators["c-import"] = ImportGenerator;
	environment.generators["import"] = ImportGenerator;

	environment.generators["defun"] = DefunGenerator;
	environment.generators["defun-local"] = DefunGenerator;
	environment.generators["defun-comptime"] = DefunGenerator;
	environment.generators["defun-nodecl"] = DefunGenerator;

	environment.generators["def-function-signature"] = DefFunctionSignatureGenerator;
	environment.generators["def-function-signature-global"] = DefFunctionSignatureGenerator;

	environment.generators["def-type-alias"] = DefTypeAliasGenerator;
	environment.generators["def-type-alias-global"] = DefTypeAliasGenerator;

	environment.generators["defmacro"] = DefMacroGenerator;
	environment.generators["defgenerator"] = DefGeneratorGenerator;

	environment.generators["defstruct"] = DefStructGenerator;
	environment.generators["defstruct-local"] = DefStructGenerator;

	environment.generators["var"] = VariableDeclarationGenerator;
	environment.generators["var-global"] = VariableDeclarationGenerator;
	environment.generators["var-static"] = VariableDeclarationGenerator;

	environment.generators["at"] = ArrayAccessGenerator;

	environment.generators["if"] = IfGenerator;
	environment.generators["cond"] = ConditionGenerator;

	environment.generators["c-preprocessor-define"] = CPreprocessorDefineGenerator;
	environment.generators["c-preprocessor-define-global"] = CPreprocessorDefineGenerator;

	// Essentially a block comment, without messing up my highlighting and such
	environment.generators["ignore"] = IgnoreGenerator;

	// Handle complex pathing, e.g. a->b.c->d.e
	environment.generators["path"] = ObjectPathGenerator;

	environment.generators["defer"] = DeferGenerator;

	// Token manipulation
	environment.generators["tokenize-push"] = TokenizePushGenerator;

	environment.generators["rename-builtin"] = RenameBuiltinGenerator;

	environment.generators["splice-point"] = SplicePointGenerator;

	// Cakelisp options
	environment.generators["set-cakelisp-option"] = SetCakelispOption;
	environment.generators["set-module-option"] = SetModuleOption;

	// All things build
	s_deprecatedHelpStrings["skip-build"] = "you should not need to specify skip-build any more";
	environment.generators["skip-build"] = SkipBuildGenerator;

	environment.generators["add-cpp-build-dependency"] = AddDependencyGenerator;
	environment.generators["add-c-build-dependency"] = AddDependencyGenerator;
	environment.generators["add-compile-time-hook"] = AddCompileTimeHookGenerator;
	environment.generators["add-compile-time-hook-module"] = AddCompileTimeHookGenerator;
	environment.generators["add-build-options"] = AddStringOptionsGenerator;
	environment.generators["add-build-options-global"] = AddStringOptionsGenerator;
	environment.generators["add-c-search-directory-module"] = AddStringOptionsGenerator;
	environment.generators["add-c-search-directory-global"] = AddStringOptionsGenerator;
	environment.generators["add-cakelisp-search-directory"] = AddStringOptionsGenerator;
	environment.generators["add-library-search-directory"] = AddStringOptionsGenerator;
	environment.generators["add-library-runtime-search-directory"] = AddStringOptionsGenerator;
	environment.generators["add-library-dependency"] = AddStringOptionsGenerator;
	environment.generators["add-compiler-link-options"] = AddStringOptionsGenerator;
	environment.generators["add-linker-options"] = AddStringOptionsGenerator;
	environment.generators["add-static-link-objects"] = AddStringOptionsGenerator;
	environment.generators["add-build-config-label"] = AddBuildConfigLabelGenerator;

	// Compile-time conditionals, erroring, etc.
	environment.generators["comptime-error"] = ComptimeErrorGenerator;
	environment.generators["comptime-cond"] = ComptimeCondGenerator;
	environment.generators["comptime-define-symbol"] = ComptimeDefineSymbolGenerator;
	environment.generators["export"] = ExportScopeGenerator;
	environment.generators["export-and-evaluate"] = ExportScopeGenerator;

	// Dispatches based on invocation name
	const char* cStatementKeywords[] = {
	    "while", "for-in", "return", "continue", "break", "when", "unless", "array", "set", "scope",
	    "?", "new", "delete", "new-array", "delete-array",
	    // Pointers
	    "deref", "addr", "field",
	    // C++ support: calling members, calling namespace functions, scope resolution operator
	    "call-on", "call-on-ptr", "call", "in", "type-cast", "type",
	    // Boolean
	    "or", "and", "not",
	    // Bitwise
	    "bit-or", "bit-and", "bit-xor", "bit-ones-complement", "bit-<<", "bit->>",
	    // Relational
	    "=", "!=", /*"eq", "neq",*/ "<=", ">=", "<", ">",
	    // Arithmetic
	    "+", "-", "*", "/", "%", "mod", "++", "--", "incr", "decr"};
	for (size_t i = 0; i < ArraySize(cStatementKeywords); ++i)
	{
		environment.generators[cStatementKeywords[i]] = CStatementGenerator;
	}

	// Deprecated
	s_deprecatedHelpStrings["block"] = "use (scope) instead";
	environment.generators["block"] = DeprecatedGenerator;

	s_deprecatedHelpStrings["nth"] = "use (at) instead";
	environment.generators["nth"] = DeprecatedGenerator;
}
