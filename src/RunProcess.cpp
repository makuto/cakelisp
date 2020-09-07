#include "RunProcess.hpp"

#include <stdio.h>
#include <vector>

#ifdef UNIX
#include <string.h>
#include <sys/types.h>  // pid
#include <sys/wait.h>   // waitpid
#include <unistd.h>     // exec, fork
#else
#error Platform support needed for running subprocesses
#endif

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
	printf("RunProcess command: ");
	for (char** arg = arguments.arguments; *arg != nullptr; ++arg)
	{
		printf("%s ", *arg);
	}
	printf("\n");

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
		perror("RunProcess fork() error: cannot create child");
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
		if (dup2(pipeFileDescriptors[PipeRead], STDIN_FILENO) == -1)
		{
			perror("RunProcess: ");
			return 1;
		}
		// Only read
		close(pipeFileDescriptors[PipeWrite]);
		printf("Created child process %d\n", pid);
		s_subprocesses.push_back({statusOut, pid});
	}

	return 1;
#endif
	return 0;
}

void waitForAllProcessesClosed(SubprocessOnOutputFunc onOutput)
{
	if (s_subprocesses.empty())
	{
		printf("No subprocesses to wait for\n");
		return;
	}
	// TODO: Don't merge all subprocesses to stdin, keep them separate to prevent multi-line errors
	// from being split
	char processOutputBuffer[1024];
	while (fgets(processOutputBuffer, sizeof(processOutputBuffer), stdin))
	{
		subprocessReceiveStdOut(processOutputBuffer);
		onOutput(processOutputBuffer);
	}

	for (Subprocess& process : s_subprocesses)
	{
#ifdef UNIX
		waitpid(process.processId, process.statusOut, 0);
#endif
	}

	s_subprocesses.clear();
}
