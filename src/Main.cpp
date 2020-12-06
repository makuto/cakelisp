#include <stdio.h>
#include <string.h>

#include <vector>

#include "FileUtilities.hpp"
#include "Logging.hpp"
#include "ModuleManager.hpp"
#include "RunProcess.hpp"
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
	    "OVERVIEW: Cakelisp\n\n"
	    "Cakelisp is a transpiler/compiler which generates C/C++ from a Lisp dialect.\n\n"
	    "Created by Macoy Madson <macoy@macoy.me>.\nhttps://macoy.me/code/macoy/cakelisp\n"
		"Copyright (c) 2020 Macoy Madson.\n\n"
	    "USAGE: cakelisp [options] <input .cake files>\nAll options must precede .cake files.\n\n"
	    "OPTIONS:\n";
	printf("%s", helpString);

	for (int optionIndex = 0; optionIndex < numOptions; ++optionIndex)
	{
		printf("  %s\n    %s\n\n", options[optionIndex].handle, options[optionIndex].help);
	}
}

void OnExecuteProcessOutput(const char* output)
{
}

int main(int numArguments, char* arguments[])
{
	bool ignoreCachedFiles = false;
	bool executeOutput = false;

	const CommandLineOption options[] = {
	    {"--ignore-cache", &ignoreCachedFiles,
	     "Prohibit skipping an operation if the resultant file is already in the cache (and the "
	     "source file hasn't been modified more recently). This is a good way to test a 'clean' "
	     "build without having to delete the Cakelisp cache directory"},
	    {"--execute", &executeOutput,
	     "If building completes successfully, run the output executable. Its working directory "
	     "will be the final location of the executable. This allows Cakelisp code to be run as if "
	     "it were a script"},
	    // Logging
	    {"--verbose-tokenization", &log.tokenization,
	     "Output details about the conversion from file text to tokens"},
	    {"--verbose-references", &log.references,
	     "Output when references to function/macro/generator invocations are created, and list all "
	     "definitions and their references"},
	    {"--verbose-dependency-propagation", &log.dependencyPropagation,
	     "Output why objects are being built (why they are required for building)"},
	    {"--verbose-build-reasons", &log.buildReasons,
	     "Output why objects are or are not being built in each compile-time build cycle"},
	    {"--verbose-build-process", &log.buildProcess,
	     "Output object statuses as they move through the compile-time pipeline"},
	    {"--verbose-compile-time-build-objects", &log.compileTimeBuildObjects,
	     "Output when a compile-time object is being built/loaded. Like --verbose-build-process, "
	     "but less verbose"},
	    {"--verbose-processes", &log.processes,
	     "Output full command lines and other information about all child processes created during "
	     "the compile-time build process"},
	    {"--verbose-file-system", &log.fileSystem,
	     "Output why files are being written, the status of comparing files, etc."},
	    {"--verbose-file-search", &log.fileSearch,
	     "Output when paths are being investigated for a file"},
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

			bool foundOption = false;
			for (int optionIndex = 0; (unsigned long)optionIndex < ArraySize(options);
			     ++optionIndex)
			{
				if (strcmp(arguments[i], options[optionIndex].handle) == 0)
				{
					*options[optionIndex].toggleOnOut = true;
					foundOption = true;
					break;
				}
			}

			if (!foundOption)
			{
				printf("Error: Unrecognized argument %s\n\n", arguments[i]);
				printHelp(options, ArraySize(options));
				return 1;
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

	// Set options after initialization
	{
		if (ignoreCachedFiles)
		{
			printf(
			    "cache will be used for output, but files from previous runs will be ignored "
			    "(--ignore-cache)\n");
			moduleManager.environment.useCachedFiles = false;
		}
	}

	for (const char* filename : filesToEvaluate)
	{
		if (!moduleManagerAddEvaluateFile(moduleManager, filename, /*moduleOut=*/nullptr))
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

	printf("\nBuild:\n");

	std::vector<std::string> builtOutputs;
	if (!moduleManagerBuild(moduleManager, builtOutputs))
	{
		moduleManagerDestroy(moduleManager);
		return 1;
	}

	if (executeOutput)
	{
		printf("\nExecute:\n");

		if (builtOutputs.empty())
		{
			printf("error: --execute: No executables were output\n");
			moduleManagerDestroy(moduleManager);
			return 1;
		}

		// TODO: Allow user to forward arguments to executable
		for (const std::string& output : builtOutputs)
		{
			RunProcessArguments arguments = {};
			// Need to use absolute path when executing
			const char* executablePath = makeAbsolutePath_Allocated(nullptr, output.c_str());
			arguments.fileToExecute = executablePath;
			const char* commandLineArguments[] = {strdup(arguments.fileToExecute), nullptr};
			arguments.arguments = commandLineArguments;
			char workingDirectory[MAX_PATH_LENGTH] = {0};
			getDirectoryFromPath(arguments.fileToExecute, workingDirectory,
			                     ArraySize(workingDirectory));
			arguments.workingDir = workingDirectory;
			int status = 0;

			if (runProcess(arguments, &status) != 0)
			{
				printf("error: execution of %s failed\n", output.c_str());
				free((void*)executablePath);
				free((void*)commandLineArguments[0]);
				moduleManagerDestroy(moduleManager);
				return 1;
			}

			waitForAllProcessesClosed(OnExecuteProcessOutput);

			free((void*)executablePath);
			free((void*)commandLineArguments[0]);

			if (status != 0)
			{
				printf("error: execution of %s returned non-zero exit code %d\n", output.c_str(),
				       status);
				moduleManagerDestroy(moduleManager);
				// Why not return the exit code? Because some exit codes end up becoming 0 after the
				// mod 256. I'm not really sure how other programs handle this
				return 1;
			}
		}
	}

	moduleManagerDestroy(moduleManager);
	return 0;
}
