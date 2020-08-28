#pragma once

#include "Tokenizer.hpp"

#include <vector>
#include <string>

struct GenerateOperation
{
	std::string output;

	// Used to correlate Cakelisp code with generated output code
	const Token* startToken;
	const Token* endToken;
};

int parserGenerateCode(const char* filename, const std::vector<Token>& tokens,
                       std::vector<GenerateOperation>& operationsOut);
