#pragma once

#include <unordered_map>
#include <vector>

#include "Build.hpp"
#include "Evaluator.hpp"
#include "Exporting.hpp"
#include "ModuleManagerEnums.hpp"
#include "RunProcess.hpp"
#include "Tokenizer.hpp"

struct ModuleDependency
{
	ModuleDependencyType type;
	std::string name;
	const Token* blameToken;
};

// Always update both of these. Signature helps validate call
extern const char* g_modulePreBuildHookSignature;
typedef bool (*ModulePreBuildHook)(ModuleManager& manager, Module* module);

struct ModuleExportScope
{
	const std::vector<Token>* tokens;
	int startTokenIndex; // Start of (export) invocation, not eval statements (for easier errors)

	// Prevent double-evaluation
	std::unordered_map<std::string, int> modulesEvaluatedExport;
};

struct CakelispDeferredImport
{
	const Token* fileToImportToken;
	CakelispImportOutput outputTo;
	GeneratorOutput* spliceOutput;
	Module* importedModule;
};

// A module is typically associated with a single file. Keywords like local mean in-module only
struct Module
{
	const char* filename;
	const std::vector<Token>* tokens;
	GeneratorOutput* generatedOutput;
	std::string sourceOutputName;
	std::string headerOutputName;

	std::vector<CakelispDeferredImport> cakelispImports;

	std::vector<ModuleExportScope> exportScopes;
	// As soon as the first importer evaluates any exports from this module, we can no longer add
	// new exports, because then we would have to go back to the importing modules and re-evaluate.
	// We could make it smart enough to do this, but it doesn't seem worth the effort now
	bool exportScopesLocked;

	// This could be determined by going definition-by-definition, but I'd rather also have a quick
	// high-level version for performance and ease of use
	RequiredFeature requiredFeatures;
	RequiredFeatureReasonList requiredFeaturesReasons;

	// Build system
	std::vector<ModuleDependency> dependencies;

	std::vector<std::string> cSearchDirectories;
	std::vector<std::string> additionalBuildOptions;

	std::vector<std::string> librarySearchDirectories;
	std::vector<std::string> libraryRuntimeSearchDirectories;
	std::vector<std::string> libraryDependencies;

	// compilerLinkOptions goes to e.g. G++ to set up arguments to the actual linker.
	// toLinkerOptions is forwarded to e.g. ld directly, not to G++
	std::vector<std::string> compilerLinkOptions;
	std::vector<std::string> toLinkerOptions;

	// Do not build or link this module. Useful both for compile-time only files (which error
	// because they are empty files) and for files only evaluated for their declarations (e.g. if
	// the definitions are going to be provided via dynamic linking)
	bool skipBuild;

	// These make sense to overload if you want a compile-time dependency
	ProcessCommand compileTimeBuildCommand;
	ProcessCommand compileTimeLinkCommand;

	ProcessCommand buildTimeBuildCommand;
	// This doesn't really make sense
	// ProcessCommand buildTimeLinkCommand;

	std::vector<CompileTimeHook> preBuildHooks;
};

struct ModuleManager
{
	// Shared environment across all modules
	EvaluatorEnvironment environment;
	Token globalPseudoInvocationName;
	// Pointer only so things cannot move around
	std::vector<Module*> modules;

	// Cached directory, not necessarily the final artifacts directory (e.g. executable-output
	// option sets different location for the final executable)
	std::string buildOutputDir;

	// If an existing cached build was run, check the current build's commands against the previous
	// commands via CRC comparison. This ensures changing commands will cause rebuilds
	ArtifactCrcTable cachedCommandCrcs;
	// If any artifact no longer matches its crc in cachedCommandCrcs, the change will appear here
	ArtifactCrcTable newCommandCrcs;

	// Cause the final output to be re-linked no matter what. Useful for things which change link
	// without using build objects
	bool forceRelink;

	CAKELISP_API ~ModuleManager() = default;
};

CAKELISP_API void moduleManagerInitialize(ModuleManager& manager);
// Do not close opened dynamic libraries. Should by called by sub-instances of cakelisp instead of
// moduleManagerDestroy(), otherwise they may segfault
CAKELISP_API void moduleManagerDestroyKeepDynLibs(ModuleManager& manager);
// Note that this will close all dynamic libraries
CAKELISP_API void moduleManagerDestroy(ModuleManager& manager);

bool moduleLoadTokenizeValidate(const char* filename, const std::vector<Token>** tokensOut);
CAKELISP_API bool moduleManagerAddEvaluateFile(ModuleManager& manager, const char* filename,
                                               Module** moduleOut);
CAKELISP_API bool moduleManagerEvaluateResolveReferences(ModuleManager& manager);
CAKELISP_API bool moduleManagerWriteGeneratedOutput(ModuleManager& manager);
CAKELISP_API bool moduleManagerBuildAndLink(ModuleManager& manager,
                                            std::vector<std::string>& builtOutputs);
CAKELISP_API bool moduleManagerExecuteBuiltOutputs(ModuleManager& manager,
                                                   const std::vector<std::string>& builtOutputs);

// Initializes a normal environment and outputs all generators available to it
void listBuiltInGenerators();
