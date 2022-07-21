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
	TokenizeState_InString,
	TokenizeState_StringMerge,
	TokenizeState_StringContinue,
	TokenizeState_InQuote,
	TokenizeState_HereString,
};

int g_totalLinesTokenized = 0;

// Returns nullptr if no errors, else the error text
const char* tokenizeLine(const char* inputLine, const char* source, unsigned int lineNumber,
                         std::vector<Token>& tokensOut)
{
	// For performance estimation only
	++g_totalLinesTokenized;

	const char* A_OK = nullptr;

	TokenizeState tokenizeState = TokenizeState_Normal;

	if (!tokensOut.empty())
	{
		if (tokensOut.back().type == TokenType_StringContinue)
			tokenizeState = TokenizeState_StringContinue;
		else if (tokensOut.back().type == TokenType_StringMerge)
			tokenizeState = TokenizeState_StringMerge;
		else if (tokensOut.back().type == TokenType_HereString)
			tokenizeState = TokenizeState_HereString;
	}

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
		*contentsBufferWrite = '\0';          \
	}

	int columnStart = 0;

	for (const char* currentChar = inputLine; *currentChar != '\0'; ++currentChar)
	{
		// printf("'%c' %d\n", *currentChar, (int)tokenizeState);

		int currentColumn = currentChar - inputLine;
		bool isCommented = false;
		switch (tokenizeState)
		{
			case TokenizeState_Normal:
				// The whole rest of the line is ignored
				if (*currentChar == commentCharacter)
				{
					isCommented = true;
					break;
				}
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
				else if (*currentChar == '\'')
				{
					WriteContents(*currentChar);
					tokenizeState = TokenizeState_InQuote;
					columnStart = currentColumn;
				}
				else if (std::isspace(*currentChar))
				{
					// We could error here if the last symbol was a open paren, but we'll just
					// ignore it for now and be extra permissive
				}
				else if (*currentChar == '\\' && !tokensOut.empty() &&
				         (tokensOut.back().type == TokenType_String ||
				          tokensOut.back().type == TokenType_StringMerge))
				{
					tokensOut.back().type = TokenType_StringMerge;
					tokenizeState = TokenizeState_StringMerge;
				}
				// "Here string" is e.g. #"#Blah blah "Look ma, no escape chars!"#"#
				else if (*currentChar == '#' && *(currentChar + 1) == '\"' &&
				         *(currentChar + 2) == '#')
				{
					Token startHereString = {
					    TokenType_HereString, EmptyString,   source,
					    lineNumber,           currentColumn, currentColumn + 1};
					tokensOut.push_back(startHereString);
					currentChar += 2;
					tokenizeState = TokenizeState_HereString;
					columnStart = currentColumn + 2;
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
				if (*currentChar == '\n' || isParenthesis || std::isspace(*currentChar))
				{
					if (logging.tokenization)
						Logf("%s\n", contentsBuffer);
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
			case TokenizeState_StringContinue:
			case TokenizeState_InString:
				if (*currentChar == '"' && previousChar != '\\')
				{
					if (!tokensOut.empty())
					{
						Token& previousToken = tokensOut.back();
						if (previousToken.type == TokenType_StringMerge ||
						    previousToken.type == TokenType_StringContinue)
						{
							std::string appendString;
							CopyContentsAndReset(appendString);
							previousToken.contents.append(appendString);
							previousToken.type = TokenType_String;
							tokenizeState = TokenizeState_Normal;
							break;
						}
					}

					Token string = {TokenType_String, EmptyString, source,
					                lineNumber,       columnStart, currentColumn + 1};
					CopyContentsAndReset(string.contents);
					tokensOut.push_back(string);

					tokenizeState = TokenizeState_Normal;
				}
				else if (*currentChar == '\n' ||
				         (*currentChar == '\r' && *(currentChar + 1) == '\n'))
				{
					// Absorb newline for multi-line strings
				}
				else
				{
					WriteContents(*currentChar);
				}
				break;
			case TokenizeState_StringMerge:
				if (*currentChar == '"')
				{
					tokenizeState = TokenizeState_InString;
				}
				else if (*currentChar == ';')
				{
					isCommented = true;
				}
				else if (std::isspace(*currentChar))
				{
				}
				else
				{
					// Everything else is an error. Nothing should come after the "\" except
					// comments or the opening of the rest of the string
					return "expected \" due to previous \\, which expects a string to merge on "
					       "next line";
				}
				break;
			case TokenizeState_InQuote:
				// This is gross because it has to support all of the following:
				// 'my-lisp-symbol ...
				// 'my-lisp-symbol) ;; Where the ) marks the end of 'my-lisp-symbol
				// 'c'
				// ' '
				// ')'
				// 'c
				// '\\'
				// '\''
				// '\"'
				if (*currentChar == '\'' &&
				    (*(currentChar - 1) != '\\' || *(currentChar - 2) == '\\'))
				{
					WriteContents(*currentChar);
					// These are a special case because unlike strings, they shouldn't be quoted or
					// delimited. Not sure what the clean way to handle them is
					Token pseudoSymbol = {TokenType_Symbol, EmptyString, source,
					                      lineNumber,       columnStart, currentColumn + 1};
					CopyContentsAndReset(pseudoSymbol.contents);
					// Logf("Write C literal: <%s>\n", pseudoSymbol.contents.c_str());
					tokensOut.push_back(pseudoSymbol);
					tokenizeState = TokenizeState_Normal;
				}
				// This condition ensures we can do ')' as well as 'my-symbol), where the latter
				// ends up being a symbol 'my-symbol
				else if (*currentChar == ')')
				{
					if (*(currentChar + 1) != '\'')
					{
						Token lispStyleSymbol = {TokenType_Symbol, EmptyString, source,
						                         lineNumber,       columnStart, currentColumn + 1};
						CopyContentsAndReset(lispStyleSymbol.contents);
						tokensOut.push_back(lispStyleSymbol);
						// Logf("Write symbol: <%s>\n", lispStyleSymbol.contents.c_str());

						// We also need to push this paren (or go back one char, but I like this
						// better because it's a bit easier to follow)
						Token closeParen = {TokenType_CloseParen, EmptyString,   source,
						                    lineNumber,           currentColumn, currentColumn + 1};
						tokensOut.push_back(closeParen);
						tokenizeState = TokenizeState_Normal;
						// Log("Write close paren\n");
					}
					else
						WriteContents(*currentChar);  // Edge case for ')' C literal
				}
				// Quoted symbol that doesn't end in a quote means lisp-style symbol, not C literal
				else if (std::isspace(*currentChar))
				{
					if (previousChar == '\'')
					{
						if (*(currentChar + 1) != '\'')
							return "Multi-character char literal or empty quote symbol not allowed";
						else
							WriteContents(*currentChar);  // Edge case for ' ' C literal
					}
					else
					{
						Token lispStyleSymbol = {TokenType_Symbol, EmptyString, source,
						                         lineNumber,       columnStart, currentColumn + 1};
						CopyContentsAndReset(lispStyleSymbol.contents);
						tokensOut.push_back(lispStyleSymbol);
						tokenizeState = TokenizeState_Normal;
						// Logf("Write lisp-style symbol: <%s>\n",
						// lispStyleSymbol.contents.c_str());
					}
				}
				else
				{
					// Note: C literals can end up with more than one char between the single
					// quotes, because the \ delimiter
					WriteContents(*currentChar);
				}
				break;
			case TokenizeState_HereString:
				if (*currentChar == '#' && *(currentChar + 1) == '\"' && *(currentChar + 2) == '#')
				{
					Token& previousToken = tokensOut.back();
					if (previousToken.type != TokenType_HereString)
						return "Here String mode was entered, but previous token is not a here-string";

					std::string appendString;
					CopyContentsAndReset(appendString);
					previousToken.contents.append(appendString);
					previousToken.type = TokenType_String;
					tokenizeState = TokenizeState_Normal;
					currentChar += 2;
				}
				else if (*currentChar == '\\')
				{
					WriteContents('\\');
					WriteContents(*currentChar);
				}
				else
				{
					WriteContents(*currentChar);
				}
				break;
			default:
				return "Unknown state! Aborting";
		}

		if (isCommented)
			break;

		previousChar = *currentChar;
	}

	if (tokenizeState != TokenizeState_Normal)
	{
		switch (tokenizeState)
		{
			case TokenizeState_Symbol:
				return "Unterminated symbol (code error?)";
			case TokenizeState_StringMerge:
				break;
			case TokenizeState_StringContinue:
			{
				Token& previousToken = tokensOut.back();
				std::string appendString;
				CopyContentsAndReset(appendString);
				previousToken.contents.append(appendString);
				previousToken.type = TokenType_StringContinue;
				break;
			}
			break;
			case TokenizeState_HereString:
			{
				Token& previousToken = tokensOut.back();
				std::string appendString;
				CopyContentsAndReset(appendString);
				previousToken.contents.append(appendString);
				previousToken.type = TokenType_HereString;
				break;
			}
			case TokenizeState_InString:
			{
				if (!tokensOut.empty())
				{
					Token& previousToken = tokensOut.back();
					if (previousToken.type == TokenType_StringMerge ||
					    previousToken.type == TokenType_StringContinue)
					{
						std::string appendString;
						CopyContentsAndReset(appendString);
						previousToken.contents.append(appendString);
						previousToken.type = TokenType_StringContinue;
						tokenizeState = TokenizeState_Normal;
						break;
					}
				}

				// Tokens empty *or* last token isn't a string continuation/resume
				{
					// The columnEnd field isn't going to make sense once we add more lines to the
					// string, so just make it one wide for now
					Token string = {
					    TokenType_StringContinue, EmptyString, source, lineNumber, columnStart,
					    columnStart + 1};
					CopyContentsAndReset(string.contents);
					tokensOut.push_back(string);
				}
			}
			break;
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
		case TokenType_String:
			return "String";
		default:
			return "Unknown type";
	}
}

bool validateTokens(const std::vector<Token>& tokens)
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
		else if (token.type == TokenType_StringContinue || token.type == TokenType_StringMerge ||
		         token.type == TokenType_HereString)
		{
			ErrorAtToken(token,
			             "multi-line string malformed. Is it missing a closing quote? Does it "
			             "have a trailing \\, despite being the last string? Is it a here-string "
			             "with missing matching #\"#?");
			return false;
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

bool appendTokenToString(const Token& token, char** at, char* bufferStart, int bufferSize)
{
	char previousCharacter = 0;
	if (*at != bufferStart)
		previousCharacter = *(*at - 1);

	switch (token.type)
	{
		case TokenType_OpenParen:
			if (previousCharacter != '(' && previousCharacter != 0)
			{
				if (!writeCharToBufferErrorToken(' ', at, bufferStart, bufferSize, token))
					return false;
			}

			return writeCharToBufferErrorToken('(', at, bufferStart, bufferSize, token);
		case TokenType_CloseParen:
			return writeCharToBufferErrorToken(')', at, bufferStart, bufferSize, token);
		case TokenType_Symbol:
			// Need space after paren for symbols
			if (previousCharacter != '(' && previousCharacter != ' ' && previousCharacter != 0)
			{
				if (!writeCharToBufferErrorToken(' ', at, bufferStart, bufferSize, token))
					return false;
			}
			if (!writeStringToBufferErrorToken(token.contents.c_str(), at, bufferStart, bufferSize,
			                                   token))
				return false;
			return writeCharToBufferErrorToken(' ', at, bufferStart, bufferSize, token);
		case TokenType_String:
			// Need space after paren for strings
			if (previousCharacter != '(' && previousCharacter != ' ' && previousCharacter != 0)
			{
				if (!writeCharToBufferErrorToken(' ', at, bufferStart, bufferSize, token))
					return false;
			}
			if (!writeStringToBufferErrorToken("\\\"", at, bufferStart, bufferSize, token))
				return false;
			// TODO Need to delimit quotes properly (try e.g. "test\"" and see it break)
			if (!writeStringToBufferErrorToken(token.contents.c_str(), at, bufferStart, bufferSize,
			                                   token))
				return false;

			return writeStringToBufferErrorToken("\\\" ", at, bufferStart, bufferSize, token);
		default:
			ErrorAtToken(token, "cannot append token of this type to string");
			return false;
	}

	return false;
}

void printFormattedToken(FILE* fileOut, const Token& token)
{
	switch (token.type)
	{
		case TokenType_OpenParen:
			fprintf(fileOut, "(");
			break;
		case TokenType_CloseParen:
			fprintf(fileOut, ")");
			break;
		case TokenType_Symbol:
			fprintf(fileOut, "%s", token.contents.c_str());
			break;
		case TokenType_String:
			fprintf(fileOut, "\"%s\"", token.contents.c_str());
			break;
		default:
			fprintf(fileOut, "Unknown type");
			break;
	}
}

static void printTokensInternal(FILE* fileOut, const std::vector<Token>& tokens, bool prettyPrint)
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
			fprintf(fileOut, "\n");
			for (int i = 0; i < depth; ++i)
			{
				fprintf(fileOut, " ");
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
			fprintf(fileOut, " ");

		printFormattedToken(fileOut, token);

		previousTokenType = token.type;
	}
	fprintf(fileOut, "\n");
}

void printTokens(const std::vector<Token>& tokens)
{
	printTokensInternal(stderr, tokens, /*prettyPrint=*/false);
}

void prettyPrintTokens(const std::vector<Token>& tokens)
{
	printTokensInternal(stderr, tokens, /*prettyPrint=*/true);
}

void prettyPrintTokensToFile(FILE* file, const std::vector<Token>& tokens)
{
	printTokensInternal(file, tokens, /*prettyPrint=*/true);
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
		Logf("error: %s\n", error);
		return false;
	}
	return true;
}
