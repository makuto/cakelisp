#pragma once

#include <vector>

#include "Tokenizer.hpp"
#include "Evaluator.hpp"

enum ModuleDependencyType
{
	ModuleDependency_Cakelisp,
	ModuleDependency_Library,
	ModuleDependency_CFile
};

struct ModuleDependency
{
	ModuleDependencyType type;
	std::string name;
};

// A module is typically associated with a single file. Keywords like local mean in-module only
struct Module
{
	const char* filename;
	const std::vector<Token>* tokens;
	GeneratorOutput* generatedOutput;
	std::string sourceOutputName;
	std::string headerOutputName;
	std::vector<ModuleDependency> dependencies;
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
