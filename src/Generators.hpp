#pragma once

#include <vector>

struct EvaluatorEnvironment;
struct EvaluatorContext;
struct GeneratorOutput;
struct Token;

bool CImportGenerator(EvaluatorEnvironment& environment, const EvaluatorContext& context,
                      const std::vector<Token>& tokens, int startTokenIndex,
                      GeneratorOutput& output);
bool DefunGenerator(EvaluatorEnvironment& environment, const EvaluatorContext& context,
                    const std::vector<Token>& tokens, int startTokenIndex, GeneratorOutput& output);
bool FunctionInvocationGenerator(EvaluatorEnvironment& environment, const EvaluatorContext& context,
                                 const std::vector<Token>& tokens, int startTokenIndex,
                                 GeneratorOutput& output);

bool SquareMacro(EvaluatorEnvironment& environment, const EvaluatorContext& context,
                 const std::vector<Token>& tokens, int startTokenIndex, std::vector<Token>& output);
