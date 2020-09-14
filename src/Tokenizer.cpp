#include "Tokenizer.hpp"

#include <stdio.h>
#include <cctype>

#include "Utilities.hpp"

static const char commentCharacter = ';';

enum TokenizeState
{
	TokenizeState_Normal,
	TokenizeState_Symbol,
	TokenizeState_InString
};

// Returns nullptr if no errors, else the error text
const char* tokenizeLine(const char* inputLine, const char* source, unsigned int lineNumber,
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
					Token openParen = {TokenType_OpenParen, EmptyString,   source,
					                   lineNumber,          currentColumn, currentColumn + 1};
					tokensOut.push_back(openParen);
				}
				else if (*currentChar == ')')
				{
					Token closeParen = {TokenType_CloseParen, EmptyString,   source,
					                    lineNumber,           currentColumn, currentColumn + 1};
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
					Token symbol = {TokenType_Symbol, EmptyString, source,
					                lineNumber,       columnStart, currentColumn};
					CopyContentsAndReset(symbol.contents);
					tokensOut.push_back(symbol);

					if (*currentChar == '(')
					{
						Token openParen = {TokenType_OpenParen, EmptyString,   source,
						                   lineNumber,          currentColumn, currentColumn + 1};
						tokensOut.push_back(openParen);
					}
					else if (*currentChar == ')')
					{
						Token closeParen = {TokenType_CloseParen, EmptyString,   source,
						                    lineNumber,           currentColumn, currentColumn + 1};
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
				if (*currentChar == '"' && previousChar != '\\')
				{
					Token string = {TokenType_String, EmptyString, source,
					                lineNumber,       columnStart, currentColumn + 1};
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

bool validateParentheses(const std::vector<Token>& tokens)
{
	int nestingDepth = 0;
	const Token* lastTopLevelOpenParen = nullptr;
	for (const Token& token : tokens)
	{
		if (token.type == TokenType_OpenParen)
		{
			if (nestingDepth == 0)
				lastTopLevelOpenParen = &token;

			++nestingDepth;
		}
		else if (token.type == TokenType_CloseParen)
		{
			--nestingDepth;
			if (nestingDepth < 0)
			{
				ErrorAtToken(token,
				             "Mismatched parenthesis. Too many closing parentheses, or missing "
				             "opening parenthesies");
				return false;
			}
		}
	}

	if (nestingDepth != 0)
	{
		ErrorAtToken(
		    *lastTopLevelOpenParen,
		    "Mismatched parenthesis. Missing closing parentheses, or too many opening parentheses");
		return false;
	}

	return true;
}

void printFormattedToken(const Token& token)
{
	switch (token.type)
	{
		case TokenType_OpenParen:
			printf("(");
			break;
		case TokenType_CloseParen:
			printf(")");
			break;
		case TokenType_Symbol:
			printf("%s ", token.contents.c_str());
			break;
		case TokenType_String:
			printf("\"%s\" ", token.contents.c_str());
			break;
		default:
			printf("Unknown type");
			break;
	}
}

bool appendTokenToString(const Token& token, char** at, char* bufferStart, int bufferSize)
{
	switch (token.type)
	{
		case TokenType_OpenParen:
			return writeCharToBuffer('(', at, bufferStart, bufferSize, token);
		case TokenType_CloseParen:
			return writeCharToBuffer(')', at, bufferStart, bufferSize, token);
		case TokenType_Symbol:
			if (!writeStringToBuffer(token.contents.c_str(), at, bufferStart, bufferSize, token))
				return false;
			// Must add space after symbol to separate it from everything else
			return writeCharToBuffer(' ', at, bufferStart, bufferSize, token);
		case TokenType_String:
			if (!writeStringToBuffer("\\\"", at, bufferStart, bufferSize, token))
				return false;
			// TODO Need to delimit quotes properly (try e.g. "test\"" and see it break)
			if (!writeStringToBuffer(token.contents.c_str(), at, bufferStart, bufferSize, token))
				return false;
			// Must add space after string to separate it from everything else
			return writeStringToBuffer("\\\" ", at, bufferStart, bufferSize, token);
			break;
		default:
			ErrorAtToken(token, "cannot append token of this type to string");
			return false;
	}

	return false;
}

void printTokens(const std::vector<Token>& tokens)
{
	// Note that token parens could be invalid, so we shouldn't do things which rely on validity
	for (const Token& token : tokens)
	{
		printFormattedToken(token);
	}
	printf("\n");
}

bool writeCharToBuffer(char c, char** at, char* bufferStart, int bufferSize, const Token& token)
{
	**at = c;
	++(*at);
	char* endOfBuffer = bufferStart + bufferSize - 1;
	if (*at >= endOfBuffer)
	{
		ErrorAtTokenf(token, "buffer of size %d was too small. String will be cut off", bufferSize);
		*endOfBuffer = '\0';
		return false;
	}

	return true;
}

bool writeStringToBuffer(const char* str, char** at, char* bufferStart, int bufferSize,
                         const Token& token)
{
	for (const char* c = str; *c != '\0'; ++c)
	{
		**at = *c;
		++(*at);
		char* endOfBuffer = bufferStart + bufferSize - 1;
		if (*at >= endOfBuffer)
		{
			ErrorAtTokenf(token, "buffer of size %d was too small. String will be cut off",
			              bufferSize);
			*(endOfBuffer) = '\0';
			return false;
		}
	}

	return true;
}
