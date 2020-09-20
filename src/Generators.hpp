#pragma once

#include <vector>

struct EvaluatorEnvironment;
struct EvaluatorContext;
struct GeneratorOutput;
struct Token;
struct StringOutput;

// This is hard-coded as the catch-all unknown invocation generator
bool FunctionInvocationGenerator(EvaluatorEnvironment& environment, const EvaluatorContext& context,
                                 const std::vector<Token>& tokens, int startTokenIndex,
                                 GeneratorOutput& output);

void importFundamentalGenerators(EvaluatorEnvironment& environment);
