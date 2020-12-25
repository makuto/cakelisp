#include "RunProcess.hpp"

#include <stdio.h>

#include <vector>

#ifdef UNIX
#include <string.h>
#include <sys/types.h>  // pid
#include <sys/wait.h>   // waitpid
#include <unistd.h>     // exec, fork

#elif WINDOWS
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

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
#ifdef UNIX
	ProcessId processId;
#elif WINDOWS
	PROCESS_INFORMATION* processInfo;
#endif
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
	Logf("Failed to execute %s\n", fileToExecute);
#endif
}

void subprocessReceiveStdOut(const char* processOutputBuffer)
{
	Logf("%s", processOutputBuffer);
}

// TODO: Make separate pipe for std err?
// void subprocessReceiveStdErr(const char* processOutputBuffer)
// {
// 	Logf("%s", processOutputBuffer);
// }

int runProcess(const RunProcessArguments& arguments, int* statusOut)
{
	if (logging.processes)
	{
		Log("RunProcess command: ");
		for (const char** arg = arguments.arguments; *arg != nullptr; ++arg)
		{
			Logf("%s ", *arg);
		}
		Log("\n");
	}

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

		if (arguments.workingDir)
		{
			if (chdir(arguments.workingDir) != 0)
			{
				perror("RunProcess chdir: ");
				goto childProcessFailed;
			}

			if (logging.processes)
				Logf("Set working directory to %s\n", arguments.workingDir);
		}

		systemExecute(arguments.fileToExecute, nonConstArguments);

	childProcessFailed:
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

		if (logging.processes)
			Logf("Created child process %d\n", pid);

		std::string command = "";
		for (const char** arg = arguments.arguments; *arg != nullptr; ++arg)
		{
			command.append(*arg);
			command.append(" ");
		}

		s_subprocesses.push_back({statusOut, pid, pipeFileDescriptors[PipeRead], command});
	}

	return 0;
#elif WINDOWS
	STARTUPINFO startupInfo;
	PROCESS_INFORMATION* processInfo = new PROCESS_INFORMATION;

	ZeroMemory(&startupInfo, sizeof(startupInfo));
	startupInfo.cb = sizeof(startupInfo);
	ZeroMemory(processInfo, sizeof(PROCESS_INFORMATION));

	char* commandLineString = nullptr;
	{
		size_t commandLineLength = 0;
		for (const char** arg = arguments.arguments; *arg != nullptr; ++arg)
		{
			commandLineLength += strlen(*arg);
			// Room for space
			commandLineLength += 1;
		}

		commandLineString = (char*)calloc(commandLineLength, sizeof(char));
		commandLineString[commandLineLength] = '\0';
		char* writeHead = commandLineString;
		for (const char** arg = arguments.arguments; *arg != nullptr; ++arg)
		{
			if (!writeStringToBuffer(*arg, &writeHead, commandLineString, commandLineLength))
			{
				free(commandLineString);
				return 1;
			}
		}
	}

	// Start the child process.
	if (!CreateProcess(arguments.fileToExecute,           // No module name (use command line)
	                   commandLineString,        // Command line
	                   NULL,           // Process handle not inheritable
	                   NULL,           // Thread handle not inheritable
	                   FALSE,          // Set handle inheritance to FALSE
	                   0,              // No creation flags
	                   NULL,           // Use parent's environment block
	                   NULL,           // Use parent's starting directory
	                   &startupInfo,   // Pointer to STARTUPINFO structure
	                   processInfo))  // Pointer to PROCESS_INFORMATION structure
	{
		Logf("CreateProcess failed (%d).\n", GetLastError());
		return 1;
	}

	std::string command = "";
	for (const char** arg = arguments.arguments; *arg != nullptr; ++arg)
	{
		command.append(*arg);
		command.append(" ");
	}

	s_subprocesses.push_back({statusOut, processInfo, 0, command});
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
			Logf("%s\n", process.command.c_str());
#elif WINDOWS
		// Wait until child process exits.
		WaitForSingleObject(process.processInfo->hProcess, INFINITE);

		DWORD exit_code;
		if (!GetExitCodeProcess(process.processInfo->hProcess, &exit_code) || exit_code != 0)
			Logf("%s\n", process.command.c_str());

		*process.statusOut = exit_code;

		// Close process and thread handles.
		CloseHandle(process.processInfo->hProcess);
		CloseHandle(process.processInfo->hThread);
#endif
	}

	s_subprocesses.clear();
}

void PrintProcessArguments(const char** processArguments)
{
	for (const char** argument = processArguments; *argument; ++argument)
		Logf("%s ", *argument);
	Log("\n");
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
				Log("error: command missing input\n");
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
