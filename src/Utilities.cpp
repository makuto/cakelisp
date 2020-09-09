#include "Utilities.hpp"

#include <stdio.h>

void printIndentToDepth(int depth)
{
	for (int i = 0; i < depth; ++i)
		printf("\t");
}

FILE* fileOpen(const char* filename, const char* mode)
{
	bool verbose = false;

	FILE* file = nullptr;
	file = fopen(filename, mode);
	if (!file)
	{
		printf("Error: Could not open %s\n", filename);
		return nullptr;
	}
	else
	{
		if (verbose)
			printf("Opened %s\n", filename);
	}
	return file;
}

std::string EmptyString;
