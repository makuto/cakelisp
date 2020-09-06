#include "RunProcess.hpp"

#include <stdio.h>

#ifdef UNIX
#include <sys/types.h>  // pid
#include <sys/wait.h>   // waitpid
#include <unistd.h>     // exec, fork
#include <string.h>
#endif

#include "Utilities.hpp"

// Never returns, if success
void systemExecuteCompile(const CompilationArguments& arguments)
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

int compileFile(const CompilationArguments& arguments)
{
#ifdef UNIX
	printf("Compiling file with command:\n");
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
		systemExecuteCompile(arguments);
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
		char processOutputBuffer[1024];
		while (fgets(processOutputBuffer, sizeof(processOutputBuffer), stdin))
		{
			subprocessReceiveStdOut(processOutputBuffer);
		}
		int status;
		waitpid(pid, &status, 0);
		return status;
	}

	return 1;
#endif
	return 0;
}
