#include "Generators.hpp"

#include <string.h>

#include <algorithm>

#include "Evaluator.hpp"
#include "FileUtilities.hpp"
#include "GeneratorHelpers.hpp"
#include "GeneratorHelpersEnums.hpp"
#include "ModuleManager.hpp"
#include "Tokenizer.hpp"
#include "Utilities.hpp"

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
			    {"'cakelisp-headers-include", ProcessCommandArgumentType_CakelispHeadersInclude},
			    {"'include-search-dirs", ProcessCommandArgumentType_IncludeSearchDirs},
			    {"'additional-options", ProcessCommandArgumentType_AdditionalOptions},
			    {"'object-input", ProcessCommandArgumentType_ObjectInput},
			    {"'library-output", ProcessCommandArgumentType_DynamicLibraryOutput},
			    {"'executable-output", ProcessCommandArgumentType_ExecutableOutput},
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
					printf("\t%s\n", symbolsToCommandTypes[i].symbolName);
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
				NoteAtTokenf(pathToken,
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
	if (tokens[optionNameIndex].contents.compare("enable-hot-reloading") == 0)
	{
		int enableStateIndex =
		    getExpectedArgument("expected path", tokens, startTokenIndex, 2, endInvocationIndex);
		if (enableStateIndex == -1)
			return false;

		const Token& enableStateToken = tokens[enableStateIndex];

		if (!ExpectTokenType("enable-hot-reloading", enableStateToken, TokenType_Symbol))
			return false;

		if (enableStateToken.contents.compare("true") == 0)
			environment.enableHotReloading = true;
		else if (enableStateToken.contents.compare("false") == 0)
			environment.enableHotReloading = false;
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

	ErrorAtToken(tokens[optionNameIndex], "unrecognized option");
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
	    {"compile-time-compiler", &context.module->compileTimeBuildCommand,
	     SetProcessCommandFileToExec},
	    {"compile-time-compile-arguments", &context.module->compileTimeBuildCommand,
	     SetProcessCommandArguments},
	    {"compile-time-linker", &context.module->compileTimeLinkCommand,
	     SetProcessCommandFileToExec},
	    {"compile-time-link-arguments", &context.module->compileTimeLinkCommand,
	     SetProcessCommandArguments},
	    {"build-time-compiler", &context.module->buildTimeBuildCommand,
	     SetProcessCommandFileToExec},
	    {"build-time-compile-arguments", &context.module->buildTimeBuildCommand,
	     SetProcessCommandArguments},
	    {"build-time-linker", &context.module->buildTimeLinkCommand, SetProcessCommandFileToExec},
	    {"build-time-link-arguments", &context.module->buildTimeLinkCommand,
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

	return true;
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

			// Insert only if not already hooked
			if (FindInContainer(context.module->preBuildHooks, hookFunction) ==
			    context.module->preBuildHooks.end())
			{
				// Finally, check the signature so we can call it safely
				static std::vector<Token> expectedSignature;
				if (expectedSignature.empty())
				{
					if (!tokenizeLinePrintError(g_modulePreBuildHookSignature, __FILE__, __LINE__,
					                            expectedSignature))
						return false;
				}

				if (!CompileTimeFunctionSignatureMatches(environment, tokens[functionNameIndex],
				                                         tokens[functionNameIndex].contents.c_str(),
				                                         expectedSignature))
					return false;

				context.module->preBuildHooks.push_back((ModulePreBuildHook)hookFunction);
			}

			return true;
		}

		if (!isModuleHook && hookName.contents.compare("pre-link") == 0)
		{
			// Insert only if not already hooked
			if (FindInContainer(environment.preLinkHooks, hookFunction) ==
			    environment.preLinkHooks.end())
			{
				// Finally, check the signature so we can call it safely
				static std::vector<Token> expectedSignature;
				if (expectedSignature.empty())
				{
					if (!tokenizeLinePrintError(g_environmentPreLinkHookSignature, __FILE__,
					                            __LINE__, expectedSignature))
						return false;
				}

				if (!CompileTimeFunctionSignatureMatches(environment, tokens[functionNameIndex],
				                                         tokens[functionNameIndex].contents.c_str(),
				                                         expectedSignature))
					return false;

				environment.preLinkHooks.push_back((PreLinkHook)hookFunction);
			}

			return true;
		}

		if (!isModuleHook && hookName.contents.compare("post-references-resolved") == 0)
		{
			// Insert only if not already hooked
			if (FindInContainer(environment.postReferencesResolvedHooks, hookFunction) ==
			    environment.postReferencesResolvedHooks.end())
			{
				// Finally, check the signature so we can call it safely
				static std::vector<Token> expectedSignature;
				if (expectedSignature.empty())
				{
					if (!tokenizeLinePrintError(g_environmentPostReferencesResolvedHookSignature,
					                            __FILE__, __LINE__, expectedSignature))
						return false;
				}

				if (!CompileTimeFunctionSignatureMatches(environment, tokens[functionNameIndex],
				                                         tokens[functionNameIndex].contents.c_str(),
				                                         expectedSignature))
					return false;

				environment.postReferencesResolvedHooks.push_back(
				    (PostReferencesResolvedHook)hookFunction);
			}

			return true;
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
	             "failed to set hook. Hook name not recognized or context mismatched");
	return false;
}

bool AddCSearchDirectoryGenerator(EvaluatorEnvironment& environment,
                                  const EvaluatorContext& context, const std::vector<Token>& tokens,
                                  int startTokenIndex, GeneratorOutput& output)
{
	// Don't let the user think this function can be called during comptime
	if (!ExpectEvaluatorScope("add-c-search-directory", tokens[startTokenIndex], context,
	                          EvaluatorScope_Module))
		return false;

	if (!context.module)
	{
		ErrorAtToken(tokens[startTokenIndex], "cannot find module (internal code error?)");
		return false;
	}

	int endInvocationIndex = FindCloseParenTokenIndex(tokens, startTokenIndex);
	for (int i = startTokenIndex + 2; i < endInvocationIndex;
	     i = getNextArgument(tokens, i, endInvocationIndex))
	{
		if (!ExpectTokenType("add-c-search-directory", tokens[i], TokenType_String))
			return false;

		bool found = false;
		for (const std::string& dir : context.module->cSearchDirectories)
		{
			if (dir.compare(tokens[i].contents) == 0)
			{
				found = true;
				break;
			}
		}
		if (!found)
			context.module->cSearchDirectories.push_back(tokens[i].contents);
	}

	return true;
}

bool AddBuildOptionGenerator(EvaluatorEnvironment& environment, const EvaluatorContext& context,
                             const std::vector<Token>& tokens, int startTokenIndex,
                             GeneratorOutput& output)
{
	// Don't let the user think this function can be called during comptime
	if (!ExpectEvaluatorScope("add-build-option", tokens[startTokenIndex], context,
	                          EvaluatorScope_Module))
		return false;

	if (!context.module)
	{
		ErrorAtToken(tokens[startTokenIndex], "cannot find module (internal code error?)");
		return false;
	}

	int endInvocationIndex = FindCloseParenTokenIndex(tokens, startTokenIndex);
	for (int i = startTokenIndex + 2; i < endInvocationIndex;
	     i = getNextArgument(tokens, i, endInvocationIndex))
	{
		if (!ExpectTokenType("add-build-option", tokens[i], TokenType_String))
			return false;

		bool found = false;
		for (const std::string& option : context.module->additionalBuildOptions)
		{
			if (option.compare(tokens[i].contents) == 0)
			{
				found = true;
				break;
			}
		}
		if (!found)
			context.module->additionalBuildOptions.push_back(tokens[i].contents);
	}

	return true;
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
	CompTimeOnly
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
				             "are:\n\t&with-defs\n\t&with-decls\n\t&comptime-only\n");
				return false;
			}

			continue;
		}
		else if (!ExpectTokenType("import file", currentToken, TokenType_String) ||
		         currentToken.contents.empty())
			continue;

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
				if (!searchForFileInPaths(currentToken.contents.c_str(),
				                          /*encounteredInFile=*/currentToken.source,
				                          environment.searchPaths, environment.numSearchPaths,
				                          resolvedPathBuffer, ArraySize(resolvedPathBuffer)))
				{
					ErrorAtToken(currentToken, "file not found! Checked the following paths:");
					printf("Checked if relative to %s\n", currentToken.source);
					printf("Checked environment search paths:\n");
					for (int pathIndex = 0; pathIndex < environment.numSearchPaths; ++pathIndex)
					{
						printf("\t%s", environment.searchPaths[pathIndex]);
					}
					return false;
				}

				// Evaluate the import!
				if (!moduleManagerAddEvaluateFile(*environment.moduleManager, resolvedPathBuffer))
				{
					ErrorAtToken(currentToken, "failed to import Cakelisp module");
					return false;
				}
			}
		}

		// Comptime only means no includes in the generated file
		if (state != CompTimeOnly)
		{
			std::vector<StringOutput>& outputDestination =
			    state == WithDefinitions ? output.source : output.header;

			addStringOutput(outputDestination, "#include", StringOutMod_SpaceAfter, &currentToken);

			// #include <stdio.h> is passed in as "<stdio.h>", so we need a special case (no quotes)
			if (currentToken.contents[0] == '<')
			{
				addStringOutput(outputDestination, currentToken.contents, StringOutMod_None,
				                &currentToken);
			}
			else
			{
				if (isCakeImport)
				{
					char cakelispExtensionBuffer[MAX_PATH_LENGTH] = {0};
					// TODO: .h vs. .hpp
					PrintfBuffer(cakelispExtensionBuffer, "%s.hpp", currentToken.contents.c_str());
					addStringOutput(outputDestination, cakelispExtensionBuffer,
					                StringOutMod_SurroundWithQuotes, &currentToken);
				}
				else
				{
					addStringOutput(outputDestination, currentToken.contents,
					                StringOutMod_SurroundWithQuotes, &currentToken);
				}
			}

			addLangTokenOutput(outputDestination, StringOutMod_NewlineAfter, &currentToken);
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
	if (!ExpectEvaluatorScope("add-cpp-build-dependency", tokens[startTokenIndex], context,
	                          EvaluatorScope_Module))
		return false;

	if (!context.module)
	{
		ErrorAtToken(tokens[startTokenIndex],
		             "module cannot track dependency (potential internal error)");
		return false;
	}

	int endInvocationIndex = FindCloseParenTokenIndex(tokens, startTokenIndex);

	int invocationIndex = startTokenIndex + 1;

	int nameIndex = getExpectedArgument("expected dependency name", tokens, startTokenIndex, 1,
	                                    endInvocationIndex);
	if (nameIndex == -1)
		return false;
	if (!ExpectTokenType("add dependency", tokens[nameIndex], TokenType_String))
		return false;

	if (tokens[invocationIndex].contents.compare("add-cpp-build-dependency") == 0)
	{
		ModuleDependency newDependency = {};
		newDependency.type = ModuleDependency_CFile;
		newDependency.name = tokens[nameIndex].contents;
		context.module->dependencies.push_back(newDependency);
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
	std::vector<FunctionArgumentTokens> arguments;
	if (!parseFunctionSignature(tokens, argsIndex, arguments, returnTypeStart))
		return false;

	// Compile-time functions need to be exposed with C bindings so they can be found
	if (isCompileTime || environment.enableHotReloading)
	{
		addStringOutput(isModuleLocal ? functionOutput->source : functionOutput->header,
		                "extern \"C\"", StringOutMod_SpaceAfter, &tokens[startTokenIndex]);
	}

	// TODO: Hot-reloading functions shouldn't be declared static, right?
	if (isModuleLocal)
		addStringOutput(functionOutput->source, "static", StringOutMod_SpaceAfter,
		                &tokens[startTokenIndex]);

	int endArgsIndex = FindCloseParenTokenIndex(tokens, argsIndex);
	if (!outputFunctionReturnType(tokens, *functionOutput, returnTypeStart, startTokenIndex,
	                              endArgsIndex,
	                              /*outputSource=*/true, /*outputHeader=*/!isModuleLocal))
		return false;

	addStringOutput(functionOutput->source, nameToken.contents, StringOutMod_ConvertFunctionName,
	                &nameToken);
	if (!isModuleLocal)
		addStringOutput(functionOutput->header, nameToken.contents,
		                StringOutMod_ConvertFunctionName, &nameToken);

	addLangTokenOutput(functionOutput->source, StringOutMod_OpenParen, &argsStart);
	if (!isModuleLocal)
		addLangTokenOutput(functionOutput->header, StringOutMod_OpenParen, &argsStart);

	if (!outputFunctionArguments(tokens, *functionOutput, arguments, /*outputSource=*/true,
	                             /*outputHeader=*/!isModuleLocal))
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
	if (!isModuleLocal)
	{
		addLangTokenOutput(functionOutput->header, StringOutMod_CloseParen, &tokens[endArgsIndex]);
		// Forward declarations end with ;
		addLangTokenOutput(functionOutput->header, StringOutMod_EndStatement,
		                   &tokens[endArgsIndex]);
	}

	int startBodyIndex = endArgsIndex + 1;
	addLangTokenOutput(functionOutput->source, StringOutMod_OpenBlock, &tokens[startBodyIndex]);

	// Evaluate our body!
	EvaluatorContext bodyContext = context;
	bodyContext.scope = EvaluatorScope_Body;
	bodyContext.definitionName = &nameToken;
	// The statements will need to handle their ;
	int numErrors = EvaluateGenerateAll_Recursive(environment, bodyContext, tokens, startBodyIndex,
	                                              /*delimiterTemplate=*/nullptr, *functionOutput);
	if (numErrors)
	{
		// Don't delete compile time function output. Evaluate may have caused references to it
		return false;
	}

	addLangTokenOutput(functionOutput->source, StringOutMod_CloseBlock, &tokens[endTokenIndex]);

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
	    tokens[startTokenIndex + 1].contents.compare("def-function-signature-local") == 0;

	if (context.scope != EvaluatorScope_Module && isModuleLocal)
		NoteAtToken(tokens[startTokenIndex + 1], "no need to specify local if in body scope");

	std::vector<StringOutput>& outputDest = isModuleLocal ? output.source : output.header;

	int returnTypeStart = -1;
	std::vector<FunctionArgumentTokens> arguments;
	if (!parseFunctionSignature(tokens, argsIndex, arguments, returnTypeStart))
		return false;

	addStringOutput(outputDest, "typedef", StringOutMod_SpaceAfter, &tokens[startTokenIndex]);

	int endArgsIndex = FindCloseParenTokenIndex(tokens, argsIndex);
	if (!outputFunctionReturnType(tokens, output, returnTypeStart, startTokenIndex, endArgsIndex,
	                              /*outputSource=*/isModuleLocal, /*outputHeader=*/!isModuleLocal))
		return false;

	// Name
	addLangTokenOutput(outputDest, StringOutMod_OpenParen, &nameToken);
	addStringOutput(outputDest, "*", StringOutMod_None, &nameToken);
	addStringOutput(outputDest, nameToken.contents, StringOutMod_ConvertTypeName, &nameToken);
	addLangTokenOutput(outputDest, StringOutMod_CloseParen, &nameToken);

	addLangTokenOutput(outputDest, StringOutMod_OpenParen, &argsStart);

	if (!outputFunctionArguments(tokens, output, arguments, /*outputSource=*/isModuleLocal,
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
	int numErrors =
	    EvaluateGenerateAll_Recursive(environment, functionInvokeContext, tokens, startArgsIndex,
	                                  &argumentDelimiterTemplate, output);
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
	bool isGlobal = funcNameToken.contents.compare("global-var") == 0;
	if (isGlobal && !ExpectEvaluatorScope("global variable declaration", tokens[startTokenIndex],
	                                      context, EvaluatorScope_Module))
		return false;

	// Only necessary for static variables declared inside a function
	bool isStatic = funcNameToken.contents.compare("static-var") == 0;
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
	if (!tokenizedCTypeToString_Recursive(tokens, typeIndex,
	                                      /*allowArray=*/true, typeOutput, typeAfterNameOutput))
		return false;

	// At this point, we probably have a valid variable. Start outputting
	addModifierToStringOutput(typeOutput.back(), StringOutMod_SpaceAfter);

	// Create separate output and definition to support compile-time modification
	GeneratorOutput* variableOutput = new GeneratorOutput;
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

		// Regardless of how much the definition is modified, it will still be output at this place
		// in the module's generated file
		addSpliceOutput(output, variableOutput, &tokens[startTokenIndex]);
	}

	if (isGlobal)
		addStringOutput(variableOutput->header, "extern", StringOutMod_SpaceAfter, &tokens[startTokenIndex]);
	// else because no variable may be declared extern while also being static
	// Automatically make module-declared variables static, reserving "static-var" for functions
	else if (isStatic || context.scope == EvaluatorScope_Module)
		addStringOutput(variableOutput->source, "static", StringOutMod_SpaceAfter, &tokens[startTokenIndex]);

	// Type
	PushBackAll(variableOutput->source, typeOutput);
	if (isGlobal)
		PushBackAll(variableOutput->header, typeOutput);

	// Name
	addStringOutput(variableOutput->source, tokens[varNameIndex].contents, StringOutMod_ConvertVariableName,
	                &tokens[varNameIndex]);
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
	}

	addLangTokenOutput(variableOutput->source, StringOutMod_EndStatement, &tokens[endInvocationIndex]);
	if (isGlobal)
		addLangTokenOutput(variableOutput->header, StringOutMod_EndStatement, &tokens[endInvocationIndex]);

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
	addLangTokenOutput(compTimeOutput->source, StringOutMod_OpenBlock, &tokens[startBodyIndex]);

	// Evaluate our body!
	EvaluatorContext macroBodyContext = context;
	macroBodyContext.scope = EvaluatorScope_Body;
	// Let the reference required propagation step handle this
	macroBodyContext.isRequired = false;
	macroBodyContext.definitionName = &nameToken;
	int numErrors =
	    EvaluateGenerateAll_Recursive(environment, macroBodyContext, tokens, startBodyIndex,
	                                  /*delimiterTemplate=*/nullptr, *compTimeOutput);
	if (numErrors)
	{
		return false;
	}

	addLangTokenOutput(compTimeOutput->source, StringOutMod_CloseBlock, &tokens[endTokenIndex]);

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
	addLangTokenOutput(compTimeOutput->source, StringOutMod_OpenBlock, &tokens[startBodyIndex]);

	// Evaluate our body!
	EvaluatorContext generatorBodyContext = context;
	generatorBodyContext.scope = EvaluatorScope_Body;
	// Let the reference required propagation step handle this
	generatorBodyContext.isRequired = false;
	generatorBodyContext.definitionName = &nameToken;
	int numErrors =
	    EvaluateGenerateAll_Recursive(environment, generatorBodyContext, tokens, startBodyIndex,
	                                  /*delimiterTemplate=*/nullptr, *compTimeOutput);
	if (numErrors)
		return false;

	addLangTokenOutput(compTimeOutput->source, StringOutMod_CloseBlock, &tokens[endTokenIndex]);

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

	addStringOutput(outputDest, "struct", StringOutMod_SpaceAfter, &tokens[startTokenIndex]);

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
			if (!tokenizedCTypeToString_Recursive(tokens, currentMember.typeStart,
			                                      /*allowArray=*/true, typeOutput,
			                                      typeAfterNameOutput))
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

		addLangTokenOutput(output.source, StringOutMod_OpenBlock, &tokens[scopedBlockIndex]);
		EvaluatorContext trueBlockBodyContext = context;
		trueBlockBodyContext.scope = EvaluatorScope_Body;
		int numErrors = 0;
		if (scopedBlockIndex != blockIndex)
			numErrors = EvaluateGenerateAll_Recursive(environment, trueBlockBodyContext, tokens,
			                                          scopedBlockIndex,
			                                          /*delimiterTemplate=*/nullptr, output);
		else
			numErrors = EvaluateGenerate_Recursive(environment, trueBlockBodyContext, tokens,
			                                       scopedBlockIndex, output);
		if (numErrors)
			return false;
		addLangTokenOutput(output.source, StringOutMod_CloseBlock, &tokens[scopedBlockIndex + 1]);
	}

	// Optional false block
	int falseBlockIndex = getNextArgument(tokens, blockIndex, endInvocationIndex);
	if (falseBlockIndex < endInvocationIndex)
	{
		int scopedFalseBlockIndex = blockAbsorbScope(tokens, falseBlockIndex);

		addStringOutput(output.source, "else", StringOutMod_None, &tokens[falseBlockIndex]);

		addLangTokenOutput(output.source, StringOutMod_OpenBlock, &tokens[falseBlockIndex]);
		EvaluatorContext falseBlockBodyContext = context;
		falseBlockBodyContext.scope = EvaluatorScope_Body;
		int numErrors = 0;
		if (scopedFalseBlockIndex != falseBlockIndex)
			numErrors = EvaluateGenerateAll_Recursive(environment, falseBlockBodyContext, tokens,
			                                          scopedFalseBlockIndex,
			                                          /*delimiterTemplate=*/nullptr, output);
		else
			numErrors = EvaluateGenerate_Recursive(environment, falseBlockBodyContext, tokens,
			                                       scopedFalseBlockIndex, output);
		if (numErrors)
			return false;
		addLangTokenOutput(output.source, StringOutMod_CloseBlock, &tokens[falseBlockIndex + 1]);
	}

	int extraArgument = getNextArgument(tokens, falseBlockIndex, endInvocationIndex);
	if (extraArgument < endInvocationIndex)
	{
		ErrorAtToken(
		    tokens[extraArgument],
		    "if expects up to two blocks. If you want to have more than one statement in a block, "
		    "surround the statements in (block) or (scope), or use cond instead of if (etc.)");
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
			addLangTokenOutput(output.source, StringOutMod_OpenBlock, &tokens[blockIndex]);
			EvaluatorContext trueBlockBodyContext = context;
			trueBlockBodyContext.scope = EvaluatorScope_Body;
			int numErrors =
			    EvaluateGenerateAll_Recursive(environment, trueBlockBodyContext, tokens, blockIndex,
			                                  /*delimiterTemplate=*/nullptr, output);
			if (numErrors)
				return false;
			addLangTokenOutput(output.source, StringOutMod_CloseBlock, &tokens[blockIndex + 1]);
		}
		else
		{
			addLangTokenOutput(output.source, StringOutMod_OpenBlock,
			                   &tokens[endConditionBlockIndex]);
			addLangTokenOutput(output.source, StringOutMod_CloseBlock,
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
	int destrEndInvocationIndex = FindCloseParenTokenIndex(tokens, startTokenIndex);
	int nameIndex =
	    getExpectedArgument("name-index", tokens, startTokenIndex, 1, destrEndInvocationIndex);
	if (-1 == nameIndex)
		return false;

	int typeIndex =
	    getExpectedArgument("type-index", tokens, startTokenIndex, 2, destrEndInvocationIndex);
	if (-1 == typeIndex)
		return false;

	const Token& invocationToken = tokens[1 + startTokenIndex];

	bool isGlobal = invocationToken.contents.compare("def-type-alias-global") == 0;

	std::vector<StringOutput> typeOutput;
	std::vector<StringOutput> typeAfterNameOutput;
	if (!(tokenizedCTypeToString_Recursive(tokens, typeIndex, true, typeOutput,
	                                       typeAfterNameOutput)))
	{
		return false;
	}
	addModifierToStringOutput(typeOutput.back(), StringOutMod_SpaceAfter);

	std::vector<StringOutput>& outputDest = isGlobal ? output.header : output.source;

	addStringOutput(outputDest, "typedef", StringOutMod_SpaceAfter, &invocationToken);
	PushBackAll(outputDest, typeOutput);
	EvaluatorContext expressionContext = context;
	expressionContext.scope = EvaluatorScope_ExpressionsOnly;
	int numErrors =
	    EvaluateGenerate_Recursive(environment, expressionContext, tokens, nameIndex, output);
	if (numErrors)
		return false;
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

// I'm not too happy with this
static void tokenizeGenerateStringTokenize(const char* outputVarName, const Token& triggerToken,
                                           const char* stringToTokenize, GeneratorOutput& output)
{
	// TODO Need to gensym error, or replace with a function call
	char tokenizeLineBuffer[2048] = {0};
	PrintfBuffer(tokenizeLineBuffer,
	             "if (!tokenizeLinePrintError(\"%s\", \"%s\", %d, %s)) {return false;}",
	             stringToTokenize, triggerToken.source, triggerToken.lineNumber, outputVarName);
	addStringOutput(output.source, tokenizeLineBuffer, StringOutMod_NewlineAfter, &triggerToken);
}

bool TokenizePushGenerator(EvaluatorEnvironment& environment, const EvaluatorContext& context,
                           const std::vector<Token>& tokens, int startTokenIndex,
                           GeneratorOutput& output)
{
	if (!ExpectEvaluatorScope("tokenize-push", tokens[startTokenIndex], context,
	                          EvaluatorScope_Body))
		return false;

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

	// This is odd: convert tokens in the macro invocation into string, then the generated C++ will
	// use that string to create tokens...
	char tokenToStringBuffer[1024] = {0};
	char* tokenToStringWrite = tokenToStringBuffer;
	const Token* tokenToStringStartToken = nullptr;

	for (int i = getExpectedArgument("tokenize-push expected tokens to output", tokens,
	                                 startTokenIndex, 2, endInvocationIndex);
	     i < endInvocationIndex; ++i)
	{
		const Token& currentToken = tokens[i];
		const Token& nextToken = tokens[i + 1];
		if (currentToken.type == TokenType_OpenParen && nextToken.type == TokenType_Symbol &&
		    (nextToken.contents.compare("token-splice") == 0 ||
		     nextToken.contents.compare("token-splice-addr") == 0 ||
		     nextToken.contents.compare("token-splice-array") == 0))
		{
			// TODO: Performance: remove extra string compare
			bool isArray = nextToken.contents.compare("token-splice-array") == 0;
			bool tokenMakePointer = nextToken.contents.compare("token-splice-addr") == 0;

			if (tokenToStringWrite != tokenToStringBuffer)
			{
				*tokenToStringWrite = '\0';
				tokenizeGenerateStringTokenize(evaluateOutputTempVar.contents.c_str(),
				                               *tokenToStringStartToken, tokenToStringBuffer,
				                               output);
				tokenToStringWrite = tokenToStringBuffer;
				tokenToStringStartToken = nullptr;
			}

			// Skip invocation
			int startSpliceArgs = i + 2;
			int endSpliceIndex = FindCloseParenTokenIndex(tokens, i);
			for (int spliceArg = startSpliceArgs; spliceArg < endSpliceIndex;
			     spliceArg = getNextArgument(tokens, spliceArg, endSpliceIndex))
			{
				addStringOutput(output.source,
				                isArray ? "PushBackAll(" : "PushBackTokenExpression(",
				                StringOutMod_None, &tokens[spliceArg]);

				// Write output argument
				addStringOutput(output.source, evaluateOutputTempVar.contents, StringOutMod_None,
				                &tokens[spliceArg]);

				addLangTokenOutput(output.source, StringOutMod_ListSeparator, &tokens[spliceArg]);

				if (tokenMakePointer)
					addStringOutput(output.source, "&", StringOutMod_None, &tokens[spliceArg]);

				// Evaluate token to start output expression
				EvaluatorContext expressionContext = context;
				expressionContext.scope = EvaluatorScope_ExpressionsOnly;
				if (EvaluateGenerate_Recursive(environment, expressionContext, tokens, spliceArg,
				                               output) != 0)
					return false;

				addLangTokenOutput(output.source, StringOutMod_CloseParen, &tokens[spliceArg]);
				addLangTokenOutput(output.source, StringOutMod_EndStatement, &tokens[spliceArg]);
			}

			// Finished splice list
			i = endSpliceIndex;
		}
		else
		{
			// All other tokens are "quoted" and added to string to output at compile-time
			if (!tokenToStringStartToken)
				tokenToStringStartToken = &currentToken;

			if (!appendTokenToString(currentToken, &tokenToStringWrite, tokenToStringBuffer,
			                         sizeof(tokenToStringBuffer)))
				return false;
		}
	}

	// Finish up leftover tokens
	if (tokenToStringWrite != tokenToStringBuffer)
	{
		*tokenToStringWrite = '\0';
		tokenizeGenerateStringTokenize(evaluateOutputTempVar.contents.c_str(),
		                               *tokenToStringStartToken, tokenToStringBuffer, output);
		// We don't really need to reset it, but we will
		tokenToStringWrite = tokenToStringBuffer;
		tokenToStringStartToken = nullptr;
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

	const CStatementOperation rangeBasedFor[] = {
	    {Keyword, "for", -1},     {OpenParen, nullptr, -1},  {TypeNoArray, nullptr, 2},
	    {Keyword, " ", -1},       {Expression, nullptr, 1},  {Keyword, ":", -1},
	    {Expression, nullptr, 3}, {CloseParen, nullptr, -1}, {OpenBlock, nullptr, -1},
	    {Body, nullptr, 4},       {CloseBlock, nullptr, -1}};

	// Conditionals
	const CStatementOperation whenStatement[] = {
	    {Keyword, "if", -1},       {OpenParen, nullptr, -1}, {Expression, nullptr, 1},
	    {CloseParen, nullptr, -1}, {OpenBlock, nullptr, -1}, {Body, nullptr, 2},
	    {CloseBlock, nullptr, -1}};
	const CStatementOperation unlessStatement[] = {
	    {Keyword, "if", -1},       {OpenParen, nullptr, -1}, {KeywordNoSpace, "!", -1},
	    {OpenParen, nullptr, -1},  {Expression, nullptr, 1}, {CloseParen, nullptr, -1},
	    {CloseParen, nullptr, -1}, {OpenBlock, nullptr, -1}, {Body, nullptr, 2},
	    {CloseBlock, nullptr, -1}};

	const CStatementOperation ternaryOperatorStatement[] = {
	    {Expression /*Name*/, nullptr, 1}, {Keyword, "?", -1},
	    {Expression, nullptr, 2},          {Keyword, ":", -1},
	    {Expression, nullptr, 3},          {SmartEndStatement, nullptr, -1}};

	// Control flow
	const CStatementOperation returnStatement[] = {{Keyword, "return", -1},
	                                               {ExpressionOptional, nullptr, 1},
	                                               {SmartEndStatement, nullptr, -1}};

	const CStatementOperation continueStatement[] = {{KeywordNoSpace, "continue", -1},
	                                                 {SmartEndStatement, nullptr, -1}};

	const CStatementOperation breakStatement[] = {{KeywordNoSpace, "break", -1},
	                                              {SmartEndStatement, nullptr, -1}};

	// Arrays are handled by (new-array)
	const CStatementOperation newStatement[] = {{Keyword, "new", -1}, {TypeNoArray, nullptr, 1}};

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
	    {Expression, nullptr, 1},        {KeywordNoSpace, ".", -1},    {Expression, nullptr, 2},
	    {OpenParen, nullptr, -1},        {ExpressionList, nullptr, 3}, {CloseParen, nullptr, -1},
	    {SmartEndStatement, nullptr, -1}};

	const CStatementOperation dereferenceMemberFunctionInvocation[] = {
	    {Expression, nullptr, 1},        {KeywordNoSpace, "->", -1},   {Expression, nullptr, 2},
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
	    {OpenBlock, nullptr, -1}, {Body, nullptr, 1}, {CloseBlock, nullptr, -1}};

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

	const CStatementOperation relationalEquality[] = {{Splice, "==", 1}};
	const CStatementOperation relationalNotEqual[] = {{Splice, "!=", 1}};
	const CStatementOperation relationalLessThanEqual[] = {{Splice, "<=", 1}};
	const CStatementOperation relationalGreaterThanEqual[] = {{Splice, ">=", 1}};
	const CStatementOperation relationalLessThan[] = {{Splice, "<", 1}};
	const CStatementOperation relationalGreaterThan[] = {{Splice, ">", 1}};

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
	} statementOperators[] = {
	    {"while", whileStatement, ArraySize(whileStatement)},
	    {"for-in", rangeBasedFor, ArraySize(rangeBasedFor)},
	    {"return", returnStatement, ArraySize(returnStatement)},
	    {"continue", continueStatement, ArraySize(continueStatement)},
	    {"break", breakStatement, ArraySize(breakStatement)},
	    {"when", whenStatement, ArraySize(whenStatement)},
	    {"unless", unlessStatement, ArraySize(unlessStatement)},
	    {"array", initializerList, ArraySize(initializerList)},
	    {"set", assignmentStatement, ArraySize(assignmentStatement)},
	    // Alias of block, in case you want to be explicit. For example, creating a scope to reduce
	    // scope of variables vs. creating a block to have more than one statement in an (if) body
	    {"scope", blockStatement, ArraySize(blockStatement)},
	    {"block", blockStatement, ArraySize(blockStatement)},
	    {"?", ternaryOperatorStatement, ArraySize(ternaryOperatorStatement)},
	    {"new", newStatement, ArraySize(newStatement)},
	    // Pointers
	    {"deref", dereference, ArraySize(dereference)},
	    {"addr", addressOf, ArraySize(addressOf)},
	    {"field", field, ArraySize(field)},
	    {"on-call", memberFunctionInvocation, ArraySize(memberFunctionInvocation)},
	    {"on-call-ptr", dereferenceMemberFunctionInvocation,
	     ArraySize(dereferenceMemberFunctionInvocation)},
	    {"call", callFunctionInvocation, ArraySize(callFunctionInvocation)},
	    {"in", scopeResolution, ArraySize(scopeResolution)},
	    {"type-cast", castStatement, ArraySize(castStatement)},
	    {"type", typeStatement, ArraySize(typeStatement)},
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
	    {"incr", increment, ArraySize(increment)},
	    {"decr", decrement, ArraySize(decrement)},
	};

	for (unsigned int i = 0; i < ArraySize(statementOperators); ++i)
	{
		if (nameToken.contents.compare(statementOperators[i].name) == 0)
		{
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

	environment.generators["def-function-signature"] = DefFunctionSignatureGenerator;
	environment.generators["def-function-signature-local"] = DefFunctionSignatureGenerator;

	environment.generators["def-type-alias"] = DefTypeAliasGenerator;
	environment.generators["def-type-alias-global"] = DefTypeAliasGenerator;

	environment.generators["defmacro"] = DefMacroGenerator;
	environment.generators["defgenerator"] = DefGeneratorGenerator;

	environment.generators["defstruct"] = DefStructGenerator;
	environment.generators["defstruct-local"] = DefStructGenerator;

	environment.generators["var"] = VariableDeclarationGenerator;
	environment.generators["global-var"] = VariableDeclarationGenerator;
	environment.generators["static-var"] = VariableDeclarationGenerator;

	environment.generators["at"] = ArrayAccessGenerator;
	environment.generators["nth"] = ArrayAccessGenerator;

	environment.generators["if"] = IfGenerator;
	environment.generators["cond"] = ConditionGenerator;

	// Essentially a block comment, without messing up my highlighting and such
	environment.generators["ignore"] = IgnoreGenerator;

	// Handle complex pathing, e.g. a->b.c->d.e
	environment.generators["path"] = ObjectPathGenerator;

	// Token manipulation
	environment.generators["tokenize-push"] = TokenizePushGenerator;

	environment.generators["rename-builtin"] = RenameBuiltinGenerator;

	// Cakelisp options
	environment.generators["set-cakelisp-option"] = SetCakelispOption;
	environment.generators["set-module-option"] = SetModuleOption;

	environment.generators["skip-build"] = SkipBuildGenerator;
	environment.generators["add-cpp-build-dependency"] = AddDependencyGenerator;
	environment.generators["add-build-options"] = AddBuildOptionGenerator;
	environment.generators["add-compile-time-hook"] = AddCompileTimeHookGenerator;
	environment.generators["add-compile-time-hook-module"] = AddCompileTimeHookGenerator;
	environment.generators["add-c-search-directory"] = AddCSearchDirectoryGenerator;

	// Dispatches based on invocation name
	const char* cStatementKeywords[] = {
	    "while", "for-in", "return", "continue", "break", "when", "unless", "array", "set", "scope",
	    "block", "?", "new",
	    // Pointers
	    "deref", "addr", "field",
	    // C++ support: calling members, calling namespace functions, scope resolution operator
	    "on-call", "on-call-ptr", "call", "in", "type-cast", "type",
	    // Boolean
	    "or", "and", "not",
	    // Bitwise
	    "bit-or", "bit-and", "bit-xor", "bit-ones-complement", "bit-<<", "bit->>",
	    // Relational
	    "=", "!=", "eq", "neq", "<=", ">=", "<", ">",
	    // Arithmetic
	    "+", "-", "*", "/", "%", "mod", "++", "--", "incr", "decr"};
	for (size_t i = 0; i < ArraySize(cStatementKeywords); ++i)
	{
		environment.generators[cStatementKeywords[i]] = CStatementGenerator;
	}
}
