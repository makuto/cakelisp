#include "Tokenizer.hpp"

#include <stdio.h>
#include <cctype>

#include "Logging.hpp"
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
					if (log.tokenization)
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
			printf("%s", token.contents.c_str());
			break;
		case TokenType_String:
			printf("\"%s\"", token.contents.c_str());
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
			return writeCharToBufferErrorToken('(', at, bufferStart, bufferSize, token);
		case TokenType_CloseParen:
			return writeCharToBufferErrorToken(')', at, bufferStart, bufferSize, token);
		case TokenType_Symbol:
			if (!writeStringToBufferErrorToken(token.contents.c_str(), at, bufferStart, bufferSize,
			                                   token))
				return false;
			// Must add space after symbol to separate it from everything else
			return writeCharToBufferErrorToken(' ', at, bufferStart, bufferSize, token);
		case TokenType_String:
			if (!writeStringToBufferErrorToken("\\\"", at, bufferStart, bufferSize, token))
				return false;
			// TODO Need to delimit quotes properly (try e.g. "test\"" and see it break)
			if (!writeStringToBufferErrorToken(token.contents.c_str(), at, bufferStart, bufferSize,
			                                   token))
				return false;
			// Must add space after string to separate it from everything else
			return writeStringToBufferErrorToken("\\\" ", at, bufferStart, bufferSize, token);
			break;
		default:
			ErrorAtToken(token, "cannot append token of this type to string");
			return false;
	}

	return false;
}

static void printTokensInternal(const std::vector<Token>& tokens, bool prettyPrint)
{
	TokenType previousTokenType = TokenType_OpenParen;

	// Note that token parens could be invalid, so we shouldn't do things which rely on validity
	int depth = 0;

	for (const Token& token : tokens)
	{
		if (token.type == TokenType_OpenParen)
			depth++;
		else if (token.type == TokenType_CloseParen)
		{
			depth--;
			// Likely invalid tokens, but just ignore it; depth is for indentation only anyways
			if (depth < 0)
				depth = 0;
		}

		// Indent and estimate newlines based on parentheses
		if (prettyPrint && previousTokenType == TokenType_CloseParen &&
		    token.type == TokenType_OpenParen)
		{
			printf("\n");
			for (int i = 0; i < depth; ++i)
			{
				printf(" ");
			}
		}

		bool tokenIsSymbolOrString =
		    (token.type == TokenType_Symbol || token.type == TokenType_String);
		bool previousTokenIsSymbolOrString =
		    (previousTokenType == TokenType_Symbol || previousTokenType == TokenType_String);
		bool previousTokenIsParen =
		    (previousTokenType == TokenType_OpenParen || previousTokenType == TokenType_CloseParen);

		if ((tokenIsSymbolOrString && !previousTokenIsParen) ||
		    (tokenIsSymbolOrString && previousTokenType == TokenType_CloseParen) ||
		    (token.type == TokenType_OpenParen && previousTokenIsSymbolOrString))
			printf(" ");

		printFormattedToken(token);

		previousTokenType = token.type;
	}
	printf("\n");
}

void printTokens(const std::vector<Token>& tokens)
{
	printTokensInternal(tokens, /*prettyPrint=*/false);
}

void prettyPrintTokens(const std::vector<Token>& tokens)
{
	printTokensInternal(tokens, /*prettyPrint=*/true);
}

bool writeCharToBufferErrorToken(char c, char** at, char* bufferStart, int bufferSize,
                                 const Token& token)
{
	if (!writeCharToBuffer(c, at, bufferStart, bufferSize))
	{
		ErrorAtTokenf(token, "buffer of size %d was too small. String will be cut off", bufferSize);
		return false;
	}

	return true;
}

bool writeStringToBufferErrorToken(const char* str, char** at, char* bufferStart, int bufferSize,
                                   const Token& token)
{
	if (!writeStringToBuffer(str, at, bufferStart, bufferSize))
	{
		ErrorAtTokenf(token, "buffer of size %d was too small. String will be cut off", bufferSize);
		return false;
	}

	return true;
}

bool tokenizeLinePrintError(const char* inputLine, const char* source, unsigned int lineNumber,
                            std::vector<Token>& tokensOut)
{
	const char* error = tokenizeLine(inputLine, source, lineNumber, tokensOut);
	if (error != nullptr)
	{
		printf("error: %s\n", error);
		return false;
	}
	return true;
}
