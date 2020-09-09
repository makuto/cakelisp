#pragma once

#include <vector>

#include "Tokenizer.hpp"
#include "Evaluator.hpp"

// A module is typically associated with a single file. Keywords like local mean in-module only
struct Module
{
	std::string filename;
	const std::vector<Token>* tokens;
	GeneratorOutput* generatedOutput;
};

struct ModuleManager
{
	// Shared environment across all modules
	EvaluatorEnvironment environment;
	Token globalPseudoInvocationName;
	std::vector<Module> modules;
};

void moduleManagerInitialize(ModuleManager& manager);
void moduleManagerDestroy(ModuleManager& manager);

bool moduleLoadTokenizeValidate(const char* filename, const std::vector<Token>** tokensOut);
bool moduleManagerAddEvaluateFile(ModuleManager& manager, const char* filename);
bool moduleManagerEvaluateResolveReferences(ModuleManager& manager);
bool moduleManagerWriteGeneratedOutput(ModuleManager& manager);
