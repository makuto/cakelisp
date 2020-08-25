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
	bool verbose = false;
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
			*contentsBufferWrite = '\0';                                         \
		}                                                                        \
		else                                                                     \
		{                                                                        \
			return "String too long!";                                           \
		}                                                                        \
	}
#define CopyContentsAndReset(outputString)    \
	{                                         \
		outputString = contentsBuffer;        \
		contentsBufferWrite = contentsBuffer; \
	}

	int columnStart = 0;

	for (const char* currentChar = inputLine; *currentChar != '\0'; ++currentChar)
	{
		int currentColumn = currentChar - inputLine;

		switch (tokenizeState)
		{
			case TokenizeState_Normal:
				// The whole rest of the line is ignored
				if (*currentChar == commentCharacter)
					return A_OK;
				else if (*currentChar == '(')
				{
					Token openParen = {TokenType_OpenParen, "", lineNumber, currentColumn,
					                   currentColumn + 1};
					tokensOut.push_back(openParen);
				}
				else if (*currentChar == ')')
				{
					Token closeParen = {TokenType_CloseParen, "", lineNumber, currentColumn,
					                    currentColumn + 1};
					tokensOut.push_back(closeParen);
				}
				else if (*currentChar == '"')
				{
					tokenizeState = TokenizeState_InString;
					columnStart = currentColumn;
				}
				else if (std::isspace(*currentChar))
				{
					// We could error here if the last symbol was a open paren, but we'll just
					// ignore it for now and be extra permissive
				}
				else
				{
					// Basically anything but parens, whitespace, or quotes can be symbols!
					tokenizeState = TokenizeState_Symbol;
					columnStart = currentColumn;
					WriteContents(*currentChar);
				}
				break;
			case TokenizeState_Symbol:
			{
				bool isParenthesis = *currentChar == ')' || *currentChar == '(';
				// Finished the symbol
				if (std::isspace(*currentChar) || *currentChar == '\n' || isParenthesis)
				{
					if (verbose)
						printf("%s\n", contentsBuffer);
					Token symbol = {TokenType_Symbol, "", lineNumber, columnStart, currentColumn};
					CopyContentsAndReset(symbol.contents);
					tokensOut.push_back(symbol);

					if (*currentChar == '(')
					{
						Token openParen = {TokenType_OpenParen, "", lineNumber, currentColumn,
						                   currentColumn + 1};
						tokensOut.push_back(openParen);
					}
					else if (*currentChar == ')')
					{
						Token closeParen = {TokenType_CloseParen, "", lineNumber, currentColumn,
						                    currentColumn + 1};
						tokensOut.push_back(closeParen);
					}

					tokenizeState = TokenizeState_Normal;
				}
				else
				{
					WriteContents(*currentChar);
				}
				break;
			}
			case TokenizeState_InString:
				if (*currentChar == '"')
				{
					if (previousChar == '\\')
					{
						// Remove the delimiter from the contents
						contentsBufferWrite--;
						WriteContents(*currentChar);
					}
					else
					{
						Token string = {TokenType_String, "", lineNumber, columnStart,
						                currentColumn + 1};
						CopyContentsAndReset(string.contents);
						tokensOut.push_back(string);

						contentsBufferWrite = contentsBuffer;
						tokenizeState = TokenizeState_Normal;
					}
				}
				else
				{
					WriteContents(*currentChar);
				}
				break;
			default:
				return "Unknown state! Aborting";
		}

		previousChar = *currentChar;
	}
#undef WriteContents
#undef CopyContentsAndReset

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
		case TokenType_String:
			return "String";
		default:
			return "Unknown type";
	}
}
