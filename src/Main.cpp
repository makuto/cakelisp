#include <stdio.h>
#include <string.h>

#include <vector>

#include "ModuleManager.hpp"

int main(int argc, char* argv[])
{
	const char* helpString =
	    "OVERVIEW: Cakelisp transpiler\n"
		"Created by Macoy Madson <macoy@macoy.me>.\nhttps://macoy.me/code/macoy/cakelisp\n\n"
	    "USAGE: cakelisp <input .cake files>\n\n"
	    "OPTIONS:\n";
	if (argc != 2)
	{
		printf("Error: expected file to parse\n\n");
		printf("%s", helpString);
		return 1;
	}

	if (strcmp(argv[1], "-h") == 0 || strcmp(argv[1], "--help") == 0)
	{
		printf("%s", helpString);
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
