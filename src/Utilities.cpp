#include "Utilities.hpp"

#include <stdio.h>

#include "Logging.hpp"

std::string EmptyString;

void printIndentToDepth(int depth)
{
	for (int i = 0; i < depth; ++i)
		Log("\t");
}

FILE* fileOpen(const char* filename, const char* mode)
{
	FILE* file = nullptr;
	file = fopen(filename, mode);
	if (!file)
	{
		Logf("error: Could not open %s\n", filename);
		return nullptr;
	}
	else
	{
		if (log.fileSystem)
			Logf("Opened %s\n", filename);
	}
	return file;
}

bool writeCharToBuffer(char c, char** at, char* bufferStart, int bufferSize)
{
	**at = c;
	++(*at);
	char* endOfBuffer = bufferStart + (bufferSize - 1);
	if (*at > endOfBuffer)
	{
		Logf("error: buffer of size %d was too small. String will be cut off\n", bufferSize);
		*endOfBuffer = '\0';
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
		char* endOfBuffer = bufferStart + (bufferSize - 1);
		if (*at > endOfBuffer)
		{
			Logf("error: buffer of size %d was too small. String will be cut off\n", bufferSize);
			*(endOfBuffer) = '\0';
			return false;
		}
	}

	return true;
}
