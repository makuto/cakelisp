#pragma once

#include <vector>

struct EvaluatorEnvironment;
struct EvaluatorContext;
struct GeneratorOutput;
struct Token;

// This is hard-coded as the catch-all unknown invocation generator
bool FunctionInvocationGenerator(EvaluatorEnvironment& environment, const EvaluatorContext& context,
                                 const std::vector<Token>& tokens, int startTokenIndex,
                                 GeneratorOutput& output);

bool SquareMacro(EvaluatorEnvironment& environment, const EvaluatorContext& context,
                 const std::vector<Token>& tokens, int startTokenIndex, std::vector<Token>& output);

void importFundamentalGenerators(EvaluatorEnvironment& environment);
