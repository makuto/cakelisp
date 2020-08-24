#include "Tokenizer.hpp"

#include <stdio.h>
#include <cctype>

static const char commentCharacter = ';';

enum TokenizeState
{
	TokenizeState_Normal,
	TokenizeState_Symbol,
	TokenizeState_InString
};

// Returns nullptr if no errors, else the error text
const char* tokenizeLine(const char* inputLine, unsigned int lineNumber,
                         std::vector<Token>& tokensOut)
{
	const char* A_OK = nullptr;

	TokenizeState tokenizeState = TokenizeState_Normal;
	char previousChar = 0;

	char contentsBuffer[1024] = {0};
	char* contentsBufferWrite = contentsBuffer;
#define WriteContents(character)                                                 \
	{                                                                            \
		if (contentsBufferWrite - contentsBuffer < (long)sizeof(contentsBuffer)) \
		{                                                                        \
			*contentsBufferWrite = *currentChar;                                 \
			++contentsBufferWrite;                                               \
		}                                                                        \
		else                                                                     \
		{                                                                        \
			return "String too long!";                                           \
		}                                                                        \
	}
#define CopyContentsAndReset(outputString)    \
	{                                         \
		outputString = nullptr;               \
		contentsBufferWrite = contentsBuffer; \
	}

	for (const char* currentChar = inputLine; *currentChar != '\0'; ++currentChar)
	{
		switch (tokenizeState)
		{
			case TokenizeState_Normal:
				// The whole rest of the line is ignored
				if (*currentChar == commentCharacter)
					return A_OK;
				else if (*currentChar == '(')
				{
					Token openParen = {TokenType_OpenParen, nullptr};
					tokensOut.push_back(openParen);
				}
				else if (*currentChar == ')')
				{
					Token closeParen = {TokenType_CloseParen, nullptr};
					tokensOut.push_back(closeParen);
				}
				else if (std::isalpha(*currentChar))
				{
					tokenizeState = TokenizeState_Symbol;
					WriteContents(*currentChar);
				}
				else if (*currentChar == '"')
				{
					tokenizeState = TokenizeState_InString;
				}
				break;
			case TokenizeState_Symbol:
				// Finished the symbol
				if (*currentChar == ' ' || *currentChar == '\n')
				{
					printf("%s\n", contentsBuffer);
					Token symbol = {TokenType_Symbol, nullptr};
					CopyContentsAndReset(symbol.contents);
					tokensOut.push_back(symbol);

					tokenizeState = TokenizeState_Normal;
				}
				else
				{
					WriteContents(*currentChar);
				}
				break;
			case TokenizeState_InString:
				if (*currentChar == '"' && previousChar != '\\')
				{
					Token string = {TokenType_String, nullptr};
					CopyContentsAndReset(string.contents);
					tokensOut.push_back(string);

					contentsBufferWrite = contentsBuffer;
					tokenizeState = TokenizeState_Normal;
				}
				else
				{
					WriteContents(*currentChar);
				}
				break;
		}
	}

	if (tokenizeState != TokenizeState_Normal)
	{
		switch (tokenizeState)
		{
			case TokenizeState_Symbol:
				return "Unterminated symbol (code error?)";
			case TokenizeState_InString:
				return "Unterminated string";
			default:
				return "Unhandled unexpected state";
		}
	}
#undef WriteContents
#undef CopyContentsAndReset

	return A_OK;
}

const char* tokenTypeToString(TokenType type)
{
	switch (type)
	{
		case TokenType_OpenParen:
			return "OpenParen";
		case TokenType_CloseParen:
			return "CloseParen";
		case TokenType_Symbol:
			return "Symbol";
		case TokenType_Constant:
			return "Constant";
		case TokenType_String:
			return "String";
		default:
			return "Unknown type";
	}
}
