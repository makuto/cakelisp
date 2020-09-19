#pragma once

#include <vector>
#include <string>

#include "EvaluatorEnums.hpp"
#include "TokenEnums.hpp"

struct Token;
struct EvaluatorContext;
struct EvaluatorEnvironment;
struct GeneratorOutput;
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

// startTokenIndex should be the opening parenthesis of the array you want to retrieve arguments
// from. For example, you should pass in the opening paren of a function invocation to get its name
// as argument 0 and first arg as argument 1 This function would be simpler and faster if there was
// an actual syntax tree, because we wouldn't be repeatedly traversing all the arguments
int getArgument(const std::vector<Token>& tokens, int startTokenIndex, int desiredArgumentIndex,
                int endTokenIndex);
int getExpectedArgument(const char* message, const std::vector<Token>& tokens, int startTokenIndex,
                        int desiredArgumentIndex, int endTokenIndex);
// Expects startTokenIndex to be the invocation. The name of the invocation is included in the count
// Note: Body arguments will not work properly with this
int getNumArguments(const std::vector<Token>& tokens, int startTokenIndex, int endTokenIndex);
// Like getNumArguments, includes invocation
bool ExpectNumArguments(const std::vector<Token>& tokens, int startTokenIndex, int endTokenIndex,
                        int numExpectedArguments);
bool isLastArgument(const std::vector<Token>& tokens, int startTokenIndex, int endTokenIndex);
// There are no more arguments once this returns endArrayTokenIndex
int getNextArgument(const std::vector<Token>& tokens, int currentTokenIndex,
                    int endArrayTokenIndex);

// If the current token is a scope, skip it. This is useful when a generator has already opened a
// block, so it knows the scope comes from the generator invocation
int blockAbsorbScope(const std::vector<Token>& tokens, int startBlockIndex);

// Similar to Lisp's gensym, make a globally unique symbol for e.g. macro variables. Use prefix so
// it is still documented as to what it represents. Make sure your generated tokenToChange is
// allocated such that it won't go away until environmentDestroyInvalidateTokens() is called (i.e.
// NOT stack allocated)
void MakeUniqueSymbolName(EvaluatorEnvironment& environment, const char* prefix,
                          Token* tokenToChange);
void PushBackTokenExpression(std::vector<Token>& output, const Token* startToken);

void addModifierToStringOutput(StringOutput& operation, StringOutputModifierFlags flag);

void addStringOutput(std::vector<StringOutput>& output, const std::string& symbol,
                     StringOutputModifierFlags modifiers, const Token* startToken);
void addLangTokenOutput(std::vector<StringOutput>& output, StringOutputModifierFlags modifiers,
                        const Token* startToken);
void addSpliceOutput(std::vector<StringOutput>& output, GeneratorOutput* spliceOutput,
                     const Token* startToken);
