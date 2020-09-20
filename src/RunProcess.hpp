#pragma once

struct RunProcessArguments
{
	const char* fileToExecute;
	const char** arguments;
};

int runProcess(const RunProcessArguments& arguments, int* statusOut);

typedef void (*SubprocessOnOutputFunc)(const char* subprocessOutput);

void waitForAllProcessesClosed(SubprocessOnOutputFunc onOutput);
