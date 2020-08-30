#include "Converters.hpp"

#include "Utilities.hpp"

// TODO: safe version of strcat
#include <ctype.h>
#include <stdio.h>
#include <string.h>

bool writeCharToBuffer(char c, char** at, char* bufferStart, int bufferSize)
{
	**at = c;
	++(*at);
	if (*at >= bufferStart + bufferSize)
	{
		printf("Error: lispNameStyleToCNameStyle(): buffer too small");
		return false;
	}

	return true;
}

bool writeStringToBuffer(const char* str, char** at, char* bufferStart, int bufferSize)
{
	for (const char* c = str; *c != '\0'; ++c)
	{
		**at = *c;
		++(*at);
		if (*at >= bufferStart + bufferSize)
		{
			printf("Error: lispNameStyleToCNameStyle(): buffer too small");
			return false;
		}
	}

	return true;
}

void lispNameStyleToCNameStyle(NameStyleMode mode, const char* name, char* bufferOut,
                               int bufferOutSize)
{
	bool upcaseNextCharacter = false;
	if (mode == NameStyleMode_PascalCase)
		upcaseNextCharacter = true;

	bool isPlural = false;
	bool startsWithSymbol = false;

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
				case NameStyleMode_PascalCaseIfPlural:
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

				startsWithSymbol = true;
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
			startsWithSymbol = true;
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

	// See comment above NameStyleMode_PascalCaseIfPlural declaration
	if (isPlural && mode == NameStyleMode_PascalCaseIfPlural)
	{
		bufferOut[0] = toupper(bufferOut[0]);
	}

	// E.g. "+" becomes "add", but in special symbols translators I didn't want to have to check for
	// first character. Fix it up here
	if (startsWithSymbol && mode == NameStyleMode_CamelCase)
		bufferOut[0] = tolower(bufferOut[0]);

	*bufferWrite = '\0';
}
