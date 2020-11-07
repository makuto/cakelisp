#include "RunProcess.hpp"

#include <stdio.h>
#include <vector>

#ifdef UNIX
#include <string.h>
#include <sys/types.h>  // pid
#include <sys/wait.h>   // waitpid
#include <unistd.h>     // exec, fork
#else
#error Platform support is needed for running subprocesses
#endif

#include "Logging.hpp"
#include "Utilities.hpp"

#ifdef UNIX
typedef pid_t ProcessId;
#else
typedef int ProcessId;
#endif

struct Subprocess
{
	int* statusOut;
	ProcessId processId;
	int pipeReadFileDescriptor;
	std::string command;
};

static std::vector<Subprocess> s_subprocesses;

// Never returns, if success
void systemExecute(const char* fileToExecute, char** arguments)
{
#ifdef UNIX
	// pid_t pid;
	execvp(fileToExecute, arguments);
	perror("RunProcess execvp() error: ");
	printf("Failed to execute %s\n", fileToExecute);
#endif
}

void subprocessReceiveStdOut(const char* processOutputBuffer)
{
	printf("%s", processOutputBuffer);
}

// TODO: Make separate pipe for std err?
// void subprocessReceiveStdErr(const char* processOutputBuffer)
// {
// 	printf("%s", processOutputBuffer);
// }

int runProcess(const RunProcessArguments& arguments, int* statusOut)
{
#ifdef UNIX
	if (log.processes)
	{
		printf("RunProcess command: ");
		for (const char** arg = arguments.arguments; *arg != nullptr; ++arg)
		{
			printf("%s ", *arg);
		}
		printf("\n");
	}

	int pipeFileDescriptors[2] = {0};
	const int PipeRead = 0;
	const int PipeWrite = 1;
	if (pipe(pipeFileDescriptors) == -1)
	{
		perror("RunProcess: ");
		return 1;
	}

	pid_t pid = fork();
	if (pid == -1)
	{
		perror("RunProcess fork() error: cannot create child: ");
		return 1;
	}
	// Child
	else if (pid == 0)
	{
		// Redirect std out and err to the pipes instead
		if (dup2(pipeFileDescriptors[PipeWrite], STDOUT_FILENO) == -1 ||
		    dup2(pipeFileDescriptors[PipeWrite], STDERR_FILENO) == -1)
		{
			perror("RunProcess: ");
			return 1;
		}
		// Only write
		close(pipeFileDescriptors[PipeRead]);

		char** nonConstArguments = nullptr;
		{
			int numArgs = 0;
			for (const char** arg = arguments.arguments; *arg != nullptr; ++arg)
				++numArgs;

			// Add one for null sentinel
			nonConstArguments = new char*[numArgs + 1];
			int i = 0;
			for (const char** arg = arguments.arguments; *arg != nullptr; ++arg)
			{
				nonConstArguments[i] = strdup(*arg);
				++i;
			}

			// Null sentinel
			nonConstArguments[numArgs] = nullptr;
		}

		systemExecute(arguments.fileToExecute, nonConstArguments);

		// This shouldn't happen unless the execution failed or soemthing
		for (char** arg = nonConstArguments; *arg != nullptr; ++arg)
			delete *arg;
		delete[] nonConstArguments;

		// A failed child should not flush parent files
		_exit(EXIT_FAILURE); /*  */
	}
	// Parent
	else
	{
		// Only read
		close(pipeFileDescriptors[PipeWrite]);

		if (log.processes)
			printf("Created child process %d\n", pid);

		std::string command = "";
		for (const char** arg = arguments.arguments; *arg != nullptr; ++arg)
		{
			command.append(*arg);
			command.append(" ");
		}

		s_subprocesses.push_back({statusOut, pid, pipeFileDescriptors[PipeRead], command});
	}

	return 0;
#endif
	return 1;
}

void waitForAllProcessesClosed(SubprocessOnOutputFunc onOutput)
{
	if (s_subprocesses.empty())
		return;

	for (Subprocess& process : s_subprocesses)
	{
#ifdef UNIX
		char processOutputBuffer[1024] = {0};
		int numBytesRead =
		    read(process.pipeReadFileDescriptor, processOutputBuffer, sizeof(processOutputBuffer));
		while (numBytesRead > 0)
		{
			processOutputBuffer[numBytesRead] = '\0';
			subprocessReceiveStdOut(processOutputBuffer);
			onOutput(processOutputBuffer);
			numBytesRead = read(process.pipeReadFileDescriptor, processOutputBuffer,
			                    sizeof(processOutputBuffer));
		}

		close(process.pipeReadFileDescriptor);

		waitpid(process.processId, process.statusOut, 0);

		// It's pretty useful to see the command which resulted in failure
		if (*process.statusOut != 0)
			printf("%s\n", process.command.c_str());
#endif
	}

	s_subprocesses.clear();
}

void PrintProcessArguments(const char** processArguments)
{
	for (const char** argument = processArguments; *argument; ++argument)
		printf("%s ", *argument);
	printf("\n");
}

// The array will need to be deleted, but the array members will not
const char** MakeProcessArgumentsFromCommand(ProcessCommand& command,
                                             const ProcessCommandInput* inputs, int numInputs)
{
	std::vector<const char*> argumentsAccumulate;

	for (unsigned int i = 0; i < command.arguments.size(); ++i)
	{
		ProcessCommandArgument& argument = command.arguments[i];

		if (argument.type == ProcessCommandArgumentType_String)
			argumentsAccumulate.push_back(argument.contents.c_str());
		else
		{
			bool found = false;
			for (int input = 0; input < numInputs; ++input)
			{
				if (inputs[input].type == argument.type)
				{
					for (const char* value : inputs[input].value)
						argumentsAccumulate.push_back(value);
					found = true;
					break;
				}
			}
			if (!found)
			{
				printf("error: command missing input\n");
				return nullptr;
			}
		}
	}

	int numUserArguments = argumentsAccumulate.size();
	// +1 for file to execute
	int numFinalArguments = numUserArguments + 1;
	// +1 again for the null terminator
	const char** newArguments = (const char**)calloc(sizeof(const char*), numFinalArguments + 1);

	newArguments[0] = command.fileToExecute.c_str();
	for (int i = 1; i < numFinalArguments; ++i)
		newArguments[i] = argumentsAccumulate[i - 1];
	newArguments[numFinalArguments] = nullptr;

	return newArguments;
}

const int maxProcessesRecommendedSpawned = 8;
