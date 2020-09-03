#pragma once

#include <vector>

#include "EvaluatorEnums.hpp"
#include "TokenEnums.hpp"

struct Token;
struct EvaluatorContext;
struct StringOutput;

void StripInvocation(int& startTokenIndex, int& endTokenIndex);
int FindCloseParenTokenIndex(const std::vector<Token>& tokens, int startTokenIndex);
bool ExpectEvaluatorScope(const char* generatorName, const Token& token,
                          const EvaluatorContext& context, EvaluatorScope expectedScope);
bool IsForbiddenEvaluatorScope(const char* generatorName, const Token& token,
                               const EvaluatorContext& context, EvaluatorScope forbiddenScope);
bool ExpectTokenType(const char* generatorName, const Token& token, TokenType expectedType);
// Errors and returns false if out of invocation (or at closing paren)
bool ExpectInInvocation(const char* message, const std::vector<Token>& tokens, int indexToCheck,
                        int endInvocationIndex);
// TODO: Come up with better name
bool isSpecialSymbol(const Token& token);
// This function would be simpler and faster if there was an actual syntax tree, because we wouldn't
// be repeatedly traversing all the arguments
int getExpectedArgument(const char* message, const std::vector<Token>& tokens, int startTokenIndex,
                        int desiredArgumentIndex, int endTokenIndex);
bool isLastArgument(const std::vector<Token>& tokens, int startTokenIndex, int endTokenIndex);
void addModifierToStringOutput(StringOutput& operation, StringOutputModifierFlags flag);
