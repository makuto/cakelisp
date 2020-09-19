#include <stdio.h>
#include <string.h>

#include <vector>

#include "Logging.hpp"
#include "ModuleManager.hpp"
#include "Utilities.hpp"

struct CommandLineOption
{
	const char* handle;
	bool* toggleOnOut;
	const char* help;
};

void printHelp(const CommandLineOption* options, int numOptions)
{
	const char* helpString =
	    "OVERVIEW: Cakelisp transpiler\n"
		"Cakelisp is a transpiler which generates C/C++ from a Lisp dialect.\n"
		"Created by Macoy Madson <macoy@macoy.me>.\nhttps://macoy.me/code/macoy/cakelisp\n\n"
	    "USAGE: cakelisp [options] <input .cake files>\nAll options must precede .cake files.\n\n"
	    "OPTIONS:\n";
	printf("%s", helpString);

	for (int optionIndex = 0; optionIndex < numOptions; ++optionIndex)
	{
		printf("%s\n\t%s\n", options[optionIndex].handle, options[optionIndex].help);
	}
}

int main(int numArguments, char* arguments[])
{
	const CommandLineOption options[] = {
	    {"--verbose-tokenization", &log.tokenization,
	     "Output details about the conversion from file text to tokens"},
	    {"--verbose-references", &log.references,
	     "Output when references to function/macro/generator invocations are created"},
	    {"--verbose-dependency-propagation", &log.dependencyPropagation,
	     "Output why objects are being built (why they are required for building)"},
	    {"--verbose-build-reasons", &log.buildReasons,
	     "Output why objects are or are not being built in each compile-time build cycle"},
	    {"--verbose-build-process", &log.buildProcess,
	     "Output object statuses as they move through the compile-time pipeline"},
	    {"--verbose-processes", &log.processes,
	     "Output full command lines of all child processes created during the compile-time build "
	     "process"},
	    {"--verbose-file-system", &log.fileSystem,
	     "Output why files are being written, the status of comparing files, etc."},
	    {"--verbose-metadata", &log.metadata, "Output generated metadata"},
	};

	if (numArguments == 1)
	{
		printf("Error: expected file(s) to evaluate\n\n");
		printHelp(options, ArraySize(options));
		return 1;
	}

	int startFiles = numArguments;

	for (int i = 1; i < numArguments; ++i)
	{
		if (strcmp(arguments[i], "-h") == 0 || strcmp(arguments[i], "--help") == 0)
		{
			printHelp(options, ArraySize(options));
			return 1;
		}
		else if (arguments[i][0] != '-')
		{
			if (startFiles == numArguments)
				startFiles = i;
		}
		else
		{
			if (startFiles < numArguments)
			{
				printf("Error: Options must precede files\n\n");
				printHelp(options, ArraySize(options));
				return 1;
			}

			for (int optionIndex = 0; (unsigned long)optionIndex < ArraySize(options);
			     ++optionIndex)
			{
				if (strcmp(arguments[i], options[optionIndex].handle) == 0)
					*options[optionIndex].toggleOnOut = true;
			}
		}
	}

	std::vector<const char*> filesToEvaluate;
	for (int i = startFiles; i < numArguments; ++i)
		filesToEvaluate.push_back(arguments[i]);

	if (filesToEvaluate.empty())
	{
		printf("Error: expected file(s) to evaluate\n\n");
		printHelp(options, ArraySize(options));
		return 1;
	}

	ModuleManager moduleManager = {};
	moduleManagerInitialize(moduleManager);

	for (const char* filename : filesToEvaluate)
	{
		if (!moduleManagerAddEvaluateFile(moduleManager, filename))
		{
			moduleManagerDestroy(moduleManager);
			return 1;
		}
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

	printf("Successfully generated files\n");

	moduleManagerDestroy(moduleManager);
	return 0;
}
