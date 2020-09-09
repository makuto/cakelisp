#pragma once

#include <vector>

#include "Tokenizer.hpp"
#include "Evaluator.hpp"

// A module is typically associated with a single file. Keywords like local mean in-module only
struct Module
{
	const char* filename;
	const std::vector<Token>* tokens;
};

struct ModuleManager
{
	// Shared environment across all modules
	EvaluatorEnvironment environment;
	Token globalPseudoInvocationName;
};

void moduleManagerInitialize(ModuleManager& manager);

bool moduleLoadTokenizeValidate(const char* filename, const std::vector<Token>** tokensOut);

bool moduleManagerAddFile(ModuleManager& manager, const char* filename);
