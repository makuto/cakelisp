#include <stdio.h>

#ifdef UNIX
#include <sys/types.h>  // pid
#include <sys/wait.h>   // waitpid
#include <unistd.h>     // exec, fork
#include <string.h>
#endif

#include "Utilities.hpp"

// Never returns, if success
void systemExecute()
{
#ifdef UNIX
	// pid_t pid;
	char fileToExec[MAX_PATH_LENGTH] = {0};
	PrintBuffer(fileToExec, "/usr/bin/clang++");
	// PrintBuffer(fileToExec, "/usr/bin/ls");

	// char arg0[64] = {0};
	// PrintBuffer(arg0, "--version");

	// If not null terminated, the call will fail
	char* arguments[] = {fileToExec, strdup("--version"), nullptr};
	printf("Running %s\n", fileToExec);
	execvp(fileToExec, arguments);
	perror("RunProcess execvp() error: ");
	printf("Failed to execute %s\n", fileToExec);
#endif
}

int main()
{
#ifdef UNIX
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
		// Only write
		close(pipeFileDescriptors[PipeRead]);
		systemExecute();
		// A failed child should not flush parent files
		_exit(EXIT_FAILURE); /*  */
	}
	// Parent
	else
	{
		// Only read
		close(pipeFileDescriptors[PipeWrite]);
		printf("Created child process %d\n", pid);
		int status;
		waitpid(pid, &status, 0);
		return status;
	}

	return 1;
#endif
	return 0;
}
