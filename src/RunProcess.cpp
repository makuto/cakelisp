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
void systemExecute(const RunProcessArguments& arguments)
{
#ifdef UNIX
	// pid_t pid;
	execvp(arguments.fileToExecute, arguments.arguments);
	perror("RunProcess execvp() error: ");
	printf("Failed to execute %s\n", arguments.fileToExecute);
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
		for (char** arg = arguments.arguments; *arg != nullptr; ++arg)
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
		if (dup2(pipeFileDescriptors[PipeWrite], STDOUT_FILENO) == -1 ||
		    dup2(pipeFileDescriptors[PipeWrite], STDERR_FILENO) == -1)
		{
			perror("RunProcess: ");
			return 1;
		}
		// Only write
		close(pipeFileDescriptors[PipeRead]);
		systemExecute(arguments);
		// A failed child should not flush parent files
		_exit(EXIT_FAILURE); /*  */
	}
	// Parent
	else
	{
		// Only read
		close(pipeFileDescriptors[PipeWrite]);
		// printf("Created child process %d\n", pid);
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
