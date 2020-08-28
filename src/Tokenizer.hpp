#pragma once

#include <vector>
#include <string>

enum TokenType
{
	TokenType_OpenParen,
	TokenType_CloseParen,
	// Note that Symbols include numerical constants
	TokenType_Symbol,
	TokenType_String
};

const char* tokenTypeToString(TokenType type);

struct Token
{
	TokenType type;
	// Only non-empty if type is ambiguous
	std::string contents;

	unsigned int lineNumber;
	// Includes quotation marks of strings. \t etc. only count as 1 column
	int columnStart;
	// Exclusive, e.g. line with "(a" would have start 0 end 1, the 'a' would have start 1 end 2
	int columnEnd;
};

void destroyToken(Token* token);

// No state past a single line means this could be called in parallel
const char* tokenizeLine(const char* inputLine, unsigned int lineNumber,
                         std::vector<Token>& tokensOut);
