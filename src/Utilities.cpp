#include "Utilities.hpp"

#include <stdio.h>

void printIndentToDepth(int depth)
{
	for (int i = 0; i < depth; ++i)
		printf("\t");
}

std::string EmptyString;
