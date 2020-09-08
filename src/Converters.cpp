#include "Converters.hpp"

#include "Utilities.hpp"

// TODO: safe version of strcat
#include <ctype.h>
#include <stdio.h>
#include <string.h>

static bool writeCharToBuffer(char c, char** at, char* bufferStart, int bufferSize)
{
	**at = c;
	++(*at);
	char* endOfBuffer = bufferStart + bufferSize - 1;
	if (*at >= endOfBuffer)
	{
		printf(
		    "\nError: lispNameStyleToCNameStyle(): buffer of size %d was too small. String will be "
		    "cut off\n",
		    bufferSize);
		*endOfBuffer = '\0';
		return false;
	}

	return true;
}

static bool writeStringToBuffer(const char* str, char** at, char* bufferStart, int bufferSize)
{
	for (const char* c = str; *c != '\0'; ++c)
	{
		**at = *c;
		++(*at);
		char* endOfBuffer = bufferStart + bufferSize - 1;
		if (*at >= endOfBuffer)
		{
			printf(
			    "\nError: lispNameStyleToCNameStyle(): buffer of size %d was too small. String "
			    "will be cut off\n",
			    bufferSize);
			*(endOfBuffer) = '\0';
			return false;
		}
	}

	return true;
}

void lispNameStyleToCNameStyle(NameStyleMode mode, const char* name, char* bufferOut,
                               int bufferOutSize)
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
					if (!writeCharToBuffer('_', &bufferWrite, bufferOut, bufferOutSize))
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
					printf(
					    "Error: lispNameStyleToCNameStyle() encountered unrecognized separator "
					    "mode\n");
					break;
			}
		}
		else if (*c == ':')
		{
			// Special case for C++ namespaces: valid only if there are two in a row
			if ((c == name && *c + 1 == ':') || (*(c + 1) == ':' || *(c - 1) == ':'))
			{
				if (!writeCharToBuffer(*c, &bufferWrite, bufferOut, bufferOutSize))
					return;
			}
			else
			{
				if (c == name)
					printf(
					    "Warning: lispNameStyleToCNameStyle() received name starting with : which "
					    "wasn't a C++-style :: scope resolution operator; is a generator wrongly "
					    "interpreting a special symbol?\n");

				requiredSymbolConversion = true;
				if (!writeStringToBuffer("Colon", &bufferWrite, bufferOut, bufferOutSize))
					return;
			}
		}
		else if (isalnum(*c) || *c == '_')
		{
			if (upcaseNextCharacter)
			{
				if (!writeCharToBuffer(toupper(*c), &bufferWrite, bufferOut, bufferOutSize))
					return;
			}
			else
			{
				if (!writeCharToBuffer(*c, &bufferWrite, bufferOut, bufferOutSize))
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
					if (!writeStringToBuffer("Add", &bufferWrite, bufferOut, bufferOutSize))
						return;
					break;
				case '-':
					if (!writeStringToBuffer("Sub", &bufferWrite, bufferOut, bufferOutSize))
						return;
					break;
				case '*':
					if (!writeStringToBuffer("Mul", &bufferWrite, bufferOut, bufferOutSize))
						return;
					break;
				case '/':
					if (!writeStringToBuffer("Div", &bufferWrite, bufferOut, bufferOutSize))
						return;
					break;
				case '%':
					if (!writeStringToBuffer("Mod", &bufferWrite, bufferOut, bufferOutSize))
						return;
					break;
				case '.':
					// TODO: Decide how to handle object pathing
					if (!writeStringToBuffer(".", &bufferWrite, bufferOut, bufferOutSize))
						return;
					break;
				default:
					printf(
					    "Error: While converting lisp-style %s to C-style, encountered character "
					    "'%c' which has no conversion equivalent. It will be replaced with "
					    "'BadChar'\n",
					    name, *c);
					if (!writeStringToBuffer("BadChar", &bufferWrite, bufferOut, bufferOutSize))
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
