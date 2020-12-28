#include "Converters.hpp"

#include "Tokenizer.hpp"
#include "Utilities.hpp"

#include <ctype.h>
#include <stdio.h>
#include <string.h>

void lispNameStyleToCNameStyle(NameStyleMode mode, const char* name, char* bufferOut,
                               int bufferOutSize, const Token& token)
{
	bool upcaseNextCharacter = false;
	bool isPlural = false;
	bool requiredSymbolConversion = false;

	char* bufferWrite = bufferOut;

	for (const char* c = name; *c != '\0'; ++c)
	{
		if (*c == '-')
		{
			switch (mode)
			{
				case NameStyleMode_Underscores:
					if (!writeCharToBufferErrorToken('_', &bufferWrite, bufferOut, bufferOutSize,
					                                 token))
						return;
					break;
				case NameStyleMode_CamelCase:
					upcaseNextCharacter = true;
					break;
				case NameStyleMode_PascalCaseIfLispy:
					isPlural = true;
					upcaseNextCharacter = true;
					break;
				case NameStyleMode_PascalCase:
					upcaseNextCharacter = true;
					break;
				default:
					ErrorAtToken(token,
					             "lispNameStyleToCNameStyle() encountered unrecognized separator "
					             "mode\n");
					break;
			}
		}
		else if (*c == ':')
		{
			// Special case for C++ namespaces: valid only if there are two in a row
			if ((c == name && *c + 1 == ':') || (*(c + 1) == ':' || *(c - 1) == ':'))
			{
				if (!writeCharToBufferErrorToken(*c, &bufferWrite, bufferOut, bufferOutSize, token))
					return;
			}
			else
			{
				if (c == name)
					ErrorAtToken(
					    token,
					    "lispNameStyleToCNameStyle() received name starting with : which "
					    "wasn't a C++-style :: scope resolution operator; is a generator wrongly "
					    "interpreting a special symbol?\n");

				requiredSymbolConversion = true;
				if (!writeStringToBufferErrorToken("Colon", &bufferWrite, bufferOut, bufferOutSize,
				                                   token))
					return;
			}
		}
		else if (isalnum(*c) || *c == '_')
		{
			if (upcaseNextCharacter)
			{
				if (!writeCharToBufferErrorToken(toupper(*c), &bufferWrite, bufferOut,
				                                 bufferOutSize, token))
					return;
			}
			else
			{
				if (!writeCharToBufferErrorToken(*c, &bufferWrite, bufferOut, bufferOutSize, token))
					return;
			}

			upcaseNextCharacter = false;
		}
		else
		{
			requiredSymbolConversion = true;
			// Name has characters C considers invalid, but Cakelisp allows
			// This is pretty odd
			switch (*c)
			{
				case '+':
					if (!writeStringToBufferErrorToken("Add", &bufferWrite, bufferOut,
					                                   bufferOutSize, token))
						return;
					break;
				case '-':
					if (!writeStringToBufferErrorToken("Sub", &bufferWrite, bufferOut,
					                                   bufferOutSize, token))
						return;
					break;
				case '*':
					if (!writeStringToBufferErrorToken("Mul", &bufferWrite, bufferOut,
					                                   bufferOutSize, token))
						return;
					break;
				case '/':
					if (!writeStringToBufferErrorToken("Div", &bufferWrite, bufferOut,
					                                   bufferOutSize, token))
						return;
					break;
				case '%':
					if (!writeStringToBufferErrorToken("Mod", &bufferWrite, bufferOut,
					                                   bufferOutSize, token))
						return;
					break;
				case '.':
					// TODO: Decide how to handle object pathing
					if (!writeStringToBufferErrorToken(".", &bufferWrite, bufferOut, bufferOutSize,
					                                   token))
						return;
					break;
				default:
					ErrorAtTokenf(
					    token,
					    "while converting lisp-style '%s' to C-style, encountered character "
					    "'%c' which has no conversion equivalent. It will be replaced with "
					    "'BadChar'",
					    name, *c);
					if (!writeStringToBufferErrorToken("BadChar", &bufferWrite, bufferOut,
					                                   bufferOutSize, token))
						return;
					break;
			}
		}
	}

	// If we have used '-' or non-C/C++ valid symbols in our name, assume it's safe to uppercase the
	// first letter. See comment above NameStyleMode_PascalCaseIfLispy declaration. PascalCase just
	// blindly changes the first letter, which can cause issues with Types
	bool isLispy = isPlural || requiredSymbolConversion;
	if (mode == NameStyleMode_PascalCase || (isLispy && mode == NameStyleMode_PascalCaseIfLispy))
	{
		//
		bufferOut[0] = toupper(bufferOut[0]);
	}

	// E.g. "+" becomes "add", but in special symbols translators I didn't want to have to check for
	// first character. Fix it up here
	if (requiredSymbolConversion && mode == NameStyleMode_CamelCase)
		bufferOut[0] = tolower(bufferOut[0]);

	*bufferWrite = '\0';
}
