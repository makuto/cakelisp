#pragma once

#include "Exporting.hpp"
#include "RunProcessEnums.hpp"

#include <string>
#include <vector>

struct RunProcessArguments
{
	const char* fileToExecute;
	// nullptr = no change (use parent process's working dir)
	const char* workingDirectory;
	const char** arguments;
};

CAKELISP_API int runProcess(const RunProcessArguments& arguments, int* statusOut);

typedef void (*SubprocessOnOutputFunc)(const char* subprocessOutput);

CAKELISP_API void waitForAllProcessesClosed(SubprocessOnOutputFunc onOutput);

//
// Helpers for programmatically constructing arguments
//

struct ProcessCommandArgument
{
	ProcessCommandArgumentType type;
	std::string contents;
};

struct ProcessCommand
{
	std::string fileToExecute;
	std::vector<ProcessCommandArgument> arguments;
};

struct ProcessCommandInput
{
	ProcessCommandArgumentType type;
	std::vector<const char*> value;
};

void PrintProcessArguments(const char** processArguments);

// The array will need to be deleted, but the array members will not
// All strings need to exist and not be moved until after you call runProcess
CAKELISP_API const char** MakeProcessArgumentsFromCommand(
    const char* fileToExecute, std::vector<ProcessCommandArgument>& arguments,
    const ProcessCommandInput* inputs, int numInputs);

extern const int maxProcessesRecommendedSpawned;
