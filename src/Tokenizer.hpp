#pragma once

#include <vector>

enum TokenType
{
	TokenType_OpenParen,
	TokenType_CloseParen,
	TokenType_Symbol,
	TokenType_Constant,
	TokenType_String
};

struct Token
{
	TokenType type;
	// Only non-null if type is ambiguous
	char* contents;
};

const char* tokenizeLine(const char* inputLine, unsigned int lineNumber,
                         std::vector<Token>& tokensOut);

const char* tokenTypeToString(TokenType type);
