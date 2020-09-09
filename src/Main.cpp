#include <stdio.h>
#include <string.h>

#include <vector>

#include "ModuleManager.hpp"

int main(int argc, char* argv[])
{
	if (argc != 2)
	{
		printf("Need to provide a file to parse\n");
		return 1;
	}

	const char* filename = argv[1];

	ModuleManager moduleManager = {};
	moduleManagerInitialize(moduleManager);

	if (!moduleManagerAddEvaluateFile(moduleManager, filename))
	{
		moduleManagerDestroy(moduleManager);
		return 1;
	}

	if (!moduleManagerEvaluateResolveReferences(moduleManager))
	{
		moduleManagerDestroy(moduleManager);
		return 1;
	}

	if (!moduleManagerWriteGeneratedOutput(moduleManager))
	{
		moduleManagerDestroy(moduleManager);
		return 1;
	}

	moduleManagerDestroy(moduleManager);
	return 0;
}
