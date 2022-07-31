#pragma once

#include <string>
#include <vector>

#include "EvaluatorEnums.hpp"
#include "Exporting.hpp"
#include "GeneratorHelpersEnums.hpp"
#include "TokenEnums.hpp"

struct Token;
struct EvaluatorContext;
struct EvaluatorEnvironment;
struct GeneratorOutput;
struct StringOutput;
struct ObjectDefinition;

void StripInvocation(int& startTokenIndex, int& endTokenIndex);
CAKELISP_API int FindCloseParenTokenIndex(const std::vector<Token>& tokens, int startTokenIndex);

CAKELISP_API bool ExpectEvaluatorScope(const char* generatorName, const Token& token,
                                       const EvaluatorContext& context, EvaluatorScope expectedScope);
bool IsForbiddenEvaluatorScope(const char* generatorName, const Token& token,
                               const EvaluatorContext& context, EvaluatorScope forbiddenScope);
CAKELISP_API bool ExpectTokenType(const char* generatorName, const Token& token,
                                  TokenType expectedType);
// Errors and returns false if out of invocation (or at closing paren)
bool ExpectInInvocation(const char* message, const std::vector<Token>& tokens, int indexToCheck,
                        int endInvocationIndex);

// Returns true if the symbol starts with :, &, or '
// TODO: Come up with better name
CAKELISP_API bool isSpecialSymbol(const Token& token);

// startTokenIndex should be the opening parenthesis of the array you want to retrieve arguments
// from. For example, you should pass in the opening paren of a function invocation to get its name
// as argument 0 and first arg as argument 1 This function would be simpler and faster if there was
// an actual syntax tree, because we wouldn't be repeatedly traversing all the arguments
// Returns -1 if argument is not within range
CAKELISP_API int getArgument(const std::vector<Token>& tokens, int startTokenIndex,
                             int desiredArgumentIndex, int endTokenIndex);
CAKELISP_API int getExpectedArgument(const char* message, const std::vector<Token>& tokens,
                                     int startTokenIndex, int desiredArgumentIndex,
                                     int endTokenIndex);
// Expects startTokenIndex to be the invocation. The name of the invocation is included in the count
// Note: Body arguments will not work properly with this
CAKELISP_API int getNumArguments(const std::vector<Token>& tokens, int startTokenIndex, int endTokenIndex);
// Like getNumArguments, includes invocation
CAKELISP_API bool ExpectNumArguments(const std::vector<Token>& tokens, int startTokenIndex,
                                     int endTokenIndex, int numExpectedArguments);
bool isLastArgument(const std::vector<Token>& tokens, int startTokenIndex, int endTokenIndex);
// There are no more arguments once this returns endArrayTokenIndex
CAKELISP_API int getNextArgument(const std::vector<Token>& tokens, int currentTokenIndex,
                                 int endArrayTokenIndex);

// If the current token is a scope, skip it. This is useful when a generator has already opened a
// block, so it knows the scope comes from the generator invocation
int blockAbsorbScope(const std::vector<Token>& tokens, int startBlockIndex);

// Like FindCloseParenTokenIndex(), only this works with nothing but a pointer
CAKELISP_API const Token* FindTokenExpressionEnd(const Token* startToken);

// For when you are within a body/rest block and only have expressions in the body
CAKELISP_API const Token* FindTokenBodyEnd(const Token* startToken);

// This is useful for copying a definition, with macros expanded, for e.g. code modification
CAKELISP_API bool CreateDefinitionCopyMacroExpanded(const ObjectDefinition& definition,
                                                    std::vector<Token>& tokensOut);

// Similar to Lisp's gensym, make a globally unique symbol for e.g. macro variables. Use prefix so
// it is still documented as to what it represents. Make sure your generated tokenToChange is
// allocated such that it won't go away until environmentDestroyInvalidateTokens() is called (i.e.
// NOT stack allocated)
// This isn't stable - if a different cakelisp command is run, that could result in different order
// of unique name acquisition
void MakeUniqueSymbolName(EvaluatorEnvironment& environment, const char* prefix,
                          Token* tokenToChange);
// This should be stable as long as the context is managed properly. Code modification may make it
// unstable unless they reset the context on reevaluate, etc.
CAKELISP_API void MakeContextUniqueSymbolName(EvaluatorEnvironment& environment,
                                              const EvaluatorContext& context, const char* prefix,
                                              Token* tokenToChange);

CAKELISP_API void PushBackTokenExpression(std::vector<Token>& output, const Token* startToken);

// e.g. output a whole block of expressions, stopping once we reach a closing paren that doesn't
// match any opening parens since startToken, or once finalToken is reached (which will be included
// if it isn't the extra closing paren). finalToken is only for the case where the expressions are
// not wrapped in a block; you should be fine to set it to the end of the tokens array
CAKELISP_API void PushBackAllTokenExpressions(std::vector<Token>& output, const Token* startToken,
                                              const Token* finalToken);

void addModifierToStringOutput(StringOutput& operation, StringOutputModifierFlags flag);

CAKELISP_API void addStringOutput(std::vector<StringOutput>& output, const std::string& symbol,
                                  StringOutputModifierFlags modifiers, const Token* startToken);
CAKELISP_API void addLangTokenOutput(std::vector<StringOutput>& output,
                                     StringOutputModifierFlags modifiers, const Token* startToken);
// Splice marker must be pushed to both source and header to preserve ordering in case spliceOutput
// has both source and header outputs
void addSpliceOutput(GeneratorOutput& output, GeneratorOutput* spliceOutput,
                     const Token* startToken);

void addSpliceOutputWithModifiers(GeneratorOutput& output, GeneratorOutput* spliceOutput,
                                  const Token* startToken, StringOutputModifierFlags modifiers);

struct FunctionArgumentTokens
{
	int startTypeIndex;
	int nameIndex;
};
CAKELISP_API bool parseFunctionSignature(const std::vector<Token>& tokens, int argsIndex,
                                         std::vector<FunctionArgumentTokens>& arguments,
                                         int& returnTypeStart, int& isVariadicIndex);
// startInvocationIndex is used for blaming on implicit return type
CAKELISP_API bool outputFunctionReturnType(EvaluatorEnvironment& environment,
                                           const EvaluatorContext& context,
                                           const std::vector<Token>& tokens,
                                           GeneratorOutput& output, int returnTypeStart,
                                           int startInvocationIndex, int endArgsIndex,
                                           bool outputSource, bool outputHeader);
CAKELISP_API bool outputFunctionArguments(EvaluatorEnvironment& environment,
                                          const EvaluatorContext& context,
                                          const std::vector<Token>& tokens, GeneratorOutput& output,
                                          const std::vector<FunctionArgumentTokens>& arguments,
                                          int isVariadicIndex, bool outputSource,
                                          bool outputHeader);

bool tokenizedCTypeToString_Recursive(EvaluatorEnvironment& environment,
                                      const EvaluatorContext& context,
                                      const std::vector<Token>& tokens, int startTokenIndex,
                                      bool allowArray, std::vector<StringOutput>& typeOutput,
                                      std::vector<StringOutput>& afterNameOutput);

bool CompileTimeFunctionSignatureMatches(EvaluatorEnvironment& environment, const Token& errorToken,
                                         const char* compileTimeFunctionName,
                                         const std::vector<Token>& expectedSignature);

// An interface for building simple generators
struct CStatementOperation
{
	CStatementOperationType type;
	const char* keywordOrSymbol;
	// 0 = operation name
	// 1 = first argument to operation (etc.)
	int argumentIndex;
};

CAKELISP_API bool CStatementOutput(EvaluatorEnvironment& environment,
                                   const EvaluatorContext& context,
                                   const std::vector<Token>& tokens, int startTokenIndex,
                                   const CStatementOperation* operation, int numOperations,
                                   GeneratorOutput& output);

// Interprets simple conditionals during compile-time. This is useful when you don't even have macro
// support yet, e.g. you need to change which compiler will even be used to make macros themselves.
// Returns whether evaluation succeeded, not the result of the conditional
bool CompileTimeEvaluateCondition(EvaluatorEnvironment& environment,
                                  const EvaluatorContext& context, const std::vector<Token>& tokens,
                                  int startTokenIndex, bool& conditionResult);

//
// Macro tokenize-push runtime
//

struct TokenizePushSpliceArgument
{
	TokenizePushSpliceArgumentType type;
	const Token* startToken;
	const std::vector<Token>* sourceTokens;
};

struct TokenizePushContext
{
	std::vector<TokenizePushSpliceArgument> spliceArguments;
};

CAKELISP_API void TokenizePushSpliceArray(TokenizePushContext* spliceContext,
                                          const std::vector<Token>* sourceTokens);
CAKELISP_API void TokenizePushSpliceAllTokenExpressions(TokenizePushContext* spliceContext,
                                                        const Token* startToken,
                                                        const std::vector<Token>* sourceTokens);
CAKELISP_API void TokenizePushSpliceTokenExpression(TokenizePushContext* spliceContext,
                                                    const Token* startToken);

CAKELISP_API bool TokenizePushExecute(EvaluatorEnvironment& environment, const char* definitionName,
                                      uint32_t tokensCrc, TokenizePushContext* spliceContext,
                                      std::vector<Token>& output);

struct Module;
CAKELISP_API void RequiresFeature(Module* module, ObjectDefinition* objectDefinition,
                                  RequiredFeature requiredFeatures, const Token* blameToken);
