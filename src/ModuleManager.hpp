#pragma once

#include <vector>

#include "ModuleManagerEnums.hpp"

#include "Evaluator.hpp"
#include "RunProcess.hpp"
#include "Tokenizer.hpp"

struct ModuleDependency
{
	ModuleDependencyType type;
	std::string name;
};

// Always update both of these. Signature helps validate call
extern const char* g_modulePreBuildHookSignature;
typedef bool (*ModulePreBuildHook)(ModuleManager& manager, Module* module);

// A module is typically associated with a single file. Keywords like local mean in-module only
struct Module
{
	const char* filename;
	const std::vector<Token>* tokens;
	GeneratorOutput* generatedOutput;

	ModuleEnvironment* moduleEnvironment;

	std::string sourceOutputName;
	std::string headerOutputName;

	// Build system
	std::vector<ModuleDependency> dependencies;
	bool skipBuild;

	// These make sense to overload if you want a compile-time dependency
	ProcessCommand compileTimeBuildCommand;
	ProcessCommand compileTimeLinkCommand;

	ProcessCommand buildTimeBuildCommand;
	ProcessCommand buildTimeLinkCommand;

	std::vector<ModulePreBuildHook> preBuildHooks;
};

struct ModuleManager
{
	// Shared environment across all modules
	EvaluatorEnvironment environment;
	Token globalPseudoInvocationName;
	// Pointer only so things cannot move around
	std::vector<Module*> modules;
};

void moduleManagerInitialize(ModuleManager& manager);
void moduleManagerDestroy(ModuleManager& manager);

bool moduleLoadTokenizeValidate(const char* filename, const std::vector<Token>** tokensOut);
bool moduleManagerAddEvaluateFile(ModuleManager& manager, const char* filename);
bool moduleManagerEvaluateResolveReferences(ModuleManager& manager);
bool moduleManagerWriteGeneratedOutput(ModuleManager& manager);
bool moduleManagerBuild(ModuleManager& manager);
