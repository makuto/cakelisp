#include <stdio.h>
#include <string.h>

#include <vector>

#include "FileUtilities.hpp"
#include "Generators.hpp"
#include "Logging.hpp"
#include "Metadata.hpp"
#include "ModuleManager.hpp"
#include "RunProcess.hpp"
#include "Utilities.hpp"

#ifdef WINDOWS
#define WIN32_LEAN_AND_MEAN
#include "windows.h"
#include "FindVisualStudio.hpp"
#endif

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
	    "Cakelisp is a transpiler/compiler which generates C/C++ from an S-expression syntax. "
	    "Cakelisp compiles and loads user code during its runtime ('comptime'), allowing it to be "
	    "extended easily. It also includes a powerful C/C++ build system.\n\n"
	    "Created by Macoy Madson <macoy@macoy.me>.\nhttps://macoy.me/code/macoy/cakelisp\n"
	    "Copyright (c) 2020 Macoy Madson.\n"
	    "Licensed under GPL-3.0-or-later.\n\n"
	    "USAGE: cakelisp [options] <input .cake files>\n\n"
	    "OPTIONS:\n";
	Logf("%s", helpString);

	for (int optionIndex = 0; optionIndex < numOptions; ++optionIndex)
	{
		Logf("  %s\n    %s\n\n", options[optionIndex].handle, options[optionIndex].help);
	}
}

static bool isOptionArgument(const char* argument)
{
	if (!argument || !argument[0])
		return false;

	return (argument[0] == '-' && argument[1] == '-');
}

int main(int numArguments, char* arguments[])
{
	bool ignoreCachedFiles = false;
	bool executeOutput = false;
	bool skipBuild = false;
	bool listBuiltInGeneratorsThenQuit = false;
	bool listBuiltInGeneratorMetadataThenQuit = false;
	bool waitForDebugger = false;
#ifdef WINDOWS
	bool listVisualStudioThenQuit = false;
#endif

	const CommandLineOption options[] = {
	    {"--ignore-cache", &ignoreCachedFiles,
	     "Prohibit skipping an operation if the resultant file is already in the cache (and the "
	     "source file hasn't been modified more recently). This is a good way to test a 'clean' "
	     "build without having to delete the Cakelisp cache directory"},
	    {"--skip-build", &skipBuild, "Only output generate files. Do not compile or link them."},
	    {"--execute", &executeOutput,
	     "If building completes successfully, run the output executable. Its working directory "
	     "will be the final location of the executable. This allows Cakelisp code to be run as if "
	     "it were a script"},
	    {"--list-built-ins", &listBuiltInGeneratorsThenQuit,
	     "List all built-in compile-time procedures, then exit. This list contains every procedure "
	     "you can possibly call, until you import more or define your own"},
	    {"--list-built-ins-details", &listBuiltInGeneratorMetadataThenQuit,
	     "List all built-in compile-time procedures and a brief explanation of each, then exit."},
	    {"--wait-for-debugger", &waitForDebugger,
	     "Wait for a debugger to be attached before starting loading and evaluation"},
#ifdef WINDOWS
	    {"--find-visual-studio", &listVisualStudioThenQuit,
	     "List where Visual Studio is and what the current Windows SDK is."},
#endif
	    // Logging
	    {"--verbose-phases", &logging.phases,
	     "Output labels for each major phase Cakelisp goes through"},
	    {"--verbose-performance", &logging.performance,
	     "Output statistics which help estimate Cakelisp's compilation performance"},
	    {"--verbose-build-omissions", &logging.buildOmissions,
	     "Output when compile-time functions are not built at all (because they were never "
	     "invoked). This can be useful if you expect your function to be referenced, but it isn't"},
	    {"--verbose-imports", &logging.imports,
	     "Output when .cake files are loaded. Also outputs when a .cake file is imported but has "
	     "already been loaded"},
	    {"--verbose-tokenization", &logging.tokenization,
	     "Output details about the conversion from file text to tokens"},
	    {"--verbose-references", &logging.references,
	     "Output when references to function/macro/generator invocations are created, and list all "
	     "definitions and their references"},
	    {"--verbose-dependency-propagation", &logging.dependencyPropagation,
	     "Output why objects are being built (why they are required for building)"},
	    {"--verbose-build-reasons", &logging.buildReasons,
	     "Output why objects are being built (i.e., why the cached version couldn't be used)"},
	    {"--verbose-compile-time-build-reasons", &logging.compileTimeBuildReasons,
	     "Output why objects are or are not being built in each compile-time build cycle"},
	    {"--verbose-build-process", &logging.buildProcess,
	     "Output object statuses as they move through the compile-time pipeline"},
	    {"--verbose-compile-time-build-objects", &logging.compileTimeBuildObjects,
	     "Output when a compile-time object is being built/loaded. Like --verbose-build-process, "
	     "but less verbose"},
	    {"--verbose-command-crcs", &logging.commandCrcs,
	     "Output CRC32s generated by process argument lists to determine whether cached files need "
	     "rebuilds"},
	    {"--verbose-compile-time-hooks", &logging.compileTimeHooks,
	     "Output when a compile-time hook is added. This is especially useful to determine the "
	     "order of execution of hooks, because each hook's priority will be output"},
	    {"--verbose-processes", &logging.processes,
	     "Output full command lines and other information about all child processes created during "
	     "the compile-time build process"},
	    {"--verbose-file-system", &logging.fileSystem,
	     "Output why files are being written, the status of comparing files, etc."},
	    {"--verbose-file-search", &logging.fileSearch,
	     "Output when paths are being investigated for a file"},
	    {"--verbose-include-scanning", &logging.includeScanning,
	     "Output when #include files are being checked for modifications. If they are modified, "
	     "the cached object files will be rebuilt"},
	    {"--verbose-strict-includes", &logging.strictIncludes,
	     "Output when #include files are not found during include scanning. The more header files "
	     "not found, the higher the chances false \"nothing to do\" builds could occur"},
	    {"--verbose-metadata", &logging.metadata, "Output generated metadata"},
	    {"--verbose-option-adding", &logging.optionAdding,
	     "Output where options are added, such as search directories, additional build options, "
	     "etc."},
	    {"--verbose-splices", &logging.splices,
	     "Output in the source file when text splices start and end. This will cause rebuilds "
	     "every time, but can be helpful in deep debugging"},
	    {"--verbose-scopes", &logging.scopes,
	     "Output in the source file when scopes are entered and exited. This will cause rebuilds "
	     "every time, but can be helpful in deep debugging. Useful when debugging unexpected "
	     "behavior of scope-related operations like defer"},
	};

	if (numArguments == 1)
	{
		Log("Error: expected file(s) to evaluate\n\n");
		printHelp(options, ArraySize(options));
		return 1;
	}

	for (int i = 1; i < numArguments; ++i)
	{
		if (strcmp(arguments[i], "-h") == 0 || strcmp(arguments[i], "--help") == 0)
		{
			printHelp(options, ArraySize(options));
			return 1;
		}
		else if (isOptionArgument(arguments[i]))
		{
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
				Logf("Error: Unrecognized argument %s\n\n", arguments[i]);
				printHelp(options, ArraySize(options));
				return 1;
			}
		}
	}

	if (waitForDebugger)
	{
#if defined(UNIX) || defined(MACOS)
		// While there is *a* way, I didn't like what I was reading, so I'm not implementing it.
		// Wait for debugger is mainly useful on Windows because Visual Studio is the best debugger,
		// but setting up a project to debug from VS is a huge PITA. It's much easier just to attach
		// to a running process, but you need time to do this manual step
		Log("error: --wait-for-debugger is not supported on *nix. Please launch from within the "
		    "debugger instead, e.g.:\ngdb --args ./bin/cakelisp test/Hello.cake\n");
		return 1;
#endif

		Log("Waiting for debugger...");
#ifdef WINDOWS
		while (!IsDebuggerPresent())
			Sleep(100);
#endif
		Log("attached\n");
	}

#ifdef WINDOWS
	if (listVisualStudioThenQuit)
	{
		Find_Result result = find_visual_studio_and_windows_sdk();
		Logf(
		    "SDK version:                  %d\n"
		    "Target platform version:      %d.%d.%d.%d\n"
		    "SDK root:         %ws\n"
		    "SDK include:      %ws\n"
		    "SDK bin:          %ws\n"
		    "SDK UM library:   %ws\n"
		    "SDK UCRT library: %ws\n"
		    "VS root path:     %ws\n"
		    "VS exe path:      %ws\n"
		    "VS include path:  %ws\n"
		    "VS library path:  %ws\n",
		    result.windows_sdk_version, result.windows_target_platform_version[0],
		    result.windows_target_platform_version[1], result.windows_target_platform_version[2],
		    result.windows_target_platform_version[3], result.windows_sdk_root,
		    result.windows_sdk_include_path, result.windows_sdk_bin_path,
		    result.windows_sdk_um_library_path, result.windows_sdk_ucrt_library_path,
		    result.vs_root_path, result.vs_exe_path, result.vs_include_path,
		    result.vs_library_path);
		// Includes
		// C:\Program Files (x86)\Microsoft Visual
		// Studio\2019\Community\VC\Tools\MSVC\14.29.30133\include C:\Program Files (x86)\Windows
		// Kits\10\include\10.0.19041.0\ucrt

		// Lib
		// C:\Program Files (x86)\Microsoft Visual Studio\2019\Community\VC\Tools\MSVC\14.29.30133\lib\x64
		// C:\Program Files (x86)\Windows Kits\10\lib\10.0.19041.0\ucrt\x64
		// C:\Program Files (x86)\Windows Kits\10\lib\10.0.19041.0\um\x64

		free_resources(&result);
		return 0;
	}
#endif

	if (listBuiltInGeneratorMetadataThenQuit)
	{
		EvaluatorEnvironment environment;
		importFundamentalGenerators(environment);
		printBuiltInGeneratorMetadata(&environment);

		return 0;
	}

	if (listBuiltInGeneratorsThenQuit)
	{
		listBuiltInGenerators();
		return 0;
	}

	std::vector<const char*> filesToEvaluate;
	for (int i = 1; i < numArguments; ++i)
	{
		if (isOptionArgument(arguments[i]))
			continue;

		filesToEvaluate.push_back(arguments[i]);
	}

	if (filesToEvaluate.empty())
	{
		Log("Error: expected file(s) to evaluate\n\n");
		printHelp(options, ArraySize(options));
		return 1;
	}

	ModuleManager moduleManager = {};
	moduleManagerInitialize(moduleManager);

	// Set options after initialization
	{
		if (ignoreCachedFiles)
		{
			Log("cache will be used for output, but files from previous runs will be ignored "
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

	if (logging.phases)
		Log("Successfully generated files\n");

	if (skipBuild)
	{
		moduleManagerDestroy(moduleManager);

		if (executeOutput)
		{
			Log("error: --skip-build is incompatible with --execute, because --execute requires an "
			    "executable to be built\n");
			return 1;
		}

		Log("Not building due to --skip-build\n");
		return 0;
	}

	if (logging.phases)
		Log("\nBuild:\n");

	std::vector<std::string> builtOutputs;
	if (!moduleManagerBuildAndLink(moduleManager, builtOutputs))
	{
		moduleManagerDestroy(moduleManager);
		return 1;
	}

	if (executeOutput)
	{
		if (!moduleManagerExecuteBuiltOutputs(moduleManager, builtOutputs))
		{
			moduleManagerDestroy(moduleManager);
			return 1;
		}
	}

	moduleManagerDestroy(moduleManager);
	return 0;
}
