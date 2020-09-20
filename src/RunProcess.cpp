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

		s_subprocesses.push_back({statusOut, pid, pipeFileDescriptors[PipeRead]});
	}

	return 1;
#endif
	return 0;
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
#endif
	}

	s_subprocesses.clear();
}
