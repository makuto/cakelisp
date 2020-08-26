#pragma once

#include "Tokenizer.hpp"

#include <vector>
#include <string>

struct GenerateOperation
{
	std::string output;
};

int parserGenerateCode(const std::vector<Token>& tokens,
                       std::vector<GenerateOperation>& operationsOut);
