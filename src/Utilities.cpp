#include "Utilities.hpp"
#include "Logging.hpp"

#include <stdio.h>

std::string EmptyString;

void printIndentToDepth(int depth)
{
	for (int i = 0; i < depth; ++i)
		printf("\t");
}

FILE* fileOpen(const char* filename, const char* mode)
{
	FILE* file = nullptr;
	file = fopen(filename, mode);
	if (!file)
	{
		printf("Error: Could not open %s\n", filename);
		return nullptr;
	}
	else
	{
		if (log.fileSystem)
			printf("Opened %s\n", filename);
	}
	return file;
}
