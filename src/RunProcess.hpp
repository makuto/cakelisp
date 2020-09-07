#pragma once

struct RunProcessArguments
{
	const char* fileToExecute;
	char** arguments;
};

typedef void (*SubprocessOnOutputFunc)(const char* subprocessOutput);

int runProcess(const RunProcessArguments& arguments, int* statusOut);
void waitForAllProcessesClosed(SubprocessOnOutputFunc onOutput);
