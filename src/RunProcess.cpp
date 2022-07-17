#include "RunProcess.hpp"

#include <stdio.h>

#include <vector>

#if defined(UNIX) || defined(MACOS)
#include <string.h>
#include <sys/types.h>  // pid
#include <sys/wait.h>   // waitpid
#include <unistd.h>     // exec, fork

#elif WINDOWS
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

// _tprintf() Remove me!
#include <tchar.h>

#else
#error Platform support is needed for running subprocesses
#endif

#include "Logging.hpp"
#include "Utilities.hpp"

#if defined(UNIX) || defined(MACOS)
typedef pid_t ProcessId;
#else
typedef int ProcessId;
#endif

struct Subprocess
{
	int* statusOut;
#if defined(UNIX) || defined(MACOS)
	ProcessId processId;
	int pipeReadFileDescriptor;
#elif WINDOWS
	PROCESS_INFORMATION* processInfo;
	// HANDLE hChildStd_IN_Wr; // Not used
	HANDLE hChildStd_OUT_Rd;
#endif
	std::string command;
};

static std::vector<Subprocess> s_subprocesses;

// Never returns, if success
void systemExecute(const char* fileToExecute, char** arguments)
{
#if defined(UNIX) || defined(MACOS)
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

int runProcess(const RunProcessArguments& arguments, int* statusOut)
{
	if (!arguments.arguments)
	{
		Log("error: runProcess() called with empty arguments. At a minimum, first argument must be "
		    "executable name\n");
		return 1;
	}

	if (logging.processes)
	{
		Log("RunProcess command: ");
		for (const char** arg = arguments.arguments; *arg != nullptr; ++arg)
		{
			Logf("%s ", *arg);
		}
		Log("\n");
	}

#if defined(UNIX) || defined(MACOS)
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
				nonConstArguments[i] = StrDuplicate(*arg);
				++i;
			}

			// Null sentinel
			nonConstArguments[numArgs] = nullptr;
		}

		if (arguments.workingDirectory)
		{
			if (chdir(arguments.workingDirectory) != 0)
			{
				Logf("error: RunProcess failed to change directory to '%s'\n",
				     arguments.workingDirectory);
				perror("RunProcess chdir");
				goto childProcessFailed;
			}

			if (logging.processes)
				Logf("Set working directory to %s\n", arguments.workingDirectory);
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
	const char* fileToExecute = arguments.fileToExecute;

	// Build a single string with all arguments
	char* commandLineString = nullptr;
	{
		size_t commandLineLength = 0;
		bool isFirstArg = true;
		for (const char** arg = arguments.arguments; *arg != nullptr; ++arg)
		{
			if (isFirstArg)
			{
				commandLineLength += strlen(fileToExecute);
				// Room for quotes
				commandLineLength += 2;
				isFirstArg = false;
			}
			else
				commandLineLength += strlen(*arg);
			// Room for space
			commandLineLength += 1;
		}

		commandLineString = (char*)calloc(commandLineLength, sizeof(char));
		commandLineString[commandLineLength - 1] = '\0';
		char* writeHead = commandLineString;
		isFirstArg = true;
		for (const char** arg = arguments.arguments; *arg != nullptr; ++arg)
		{
			// Support executable with spaces in path
			if (isFirstArg)
			{
				isFirstArg = false;
				if (!writeCharToBuffer('"', &writeHead, commandLineString, commandLineLength))
				{
					Log("error: ran out of space to write command\n");
					free(commandLineString);
					return 1;
				}
				if (!writeStringToBuffer(fileToExecute, &writeHead, commandLineString,
				                         commandLineLength))
				{
					Log("error: ran out of space to write command\n");
					free(commandLineString);
					return 1;
				}
				if (!writeCharToBuffer('"', &writeHead, commandLineString, commandLineLength))
				{
					Log("error: ran out of space to write command\n");
					free(commandLineString);
					return 1;
				}
			}
			else
			{
				if (!writeStringToBuffer(*arg, &writeHead, commandLineString, commandLineLength))
				{
					Log("error: ran out of space to write command\n");
					free(commandLineString);
					return 1;
				}
			}

			if (*(arg + 1) != nullptr)
			{
				if (!writeCharToBuffer(' ', &writeHead, commandLineString, commandLineLength))
				{
					Log("error: ran out of space to write command\n");
					free(commandLineString);
					return 1;
				}
			}
		}
	}

	if (logging.processes)
		Logf("Final command string: %s\n", commandLineString);

	// Redirect child process std in/out
	HANDLE hChildStd_IN_Rd = nullptr;
	HANDLE hChildStd_IN_Wr = nullptr;
	HANDLE hChildStd_OUT_Rd = nullptr;
	HANDLE hChildStd_OUT_Wr = nullptr;
	{
		SECURITY_ATTRIBUTES securityAttributes;
		// Set the bInheritHandle flag so pipe handles are inherited.
		securityAttributes.nLength = sizeof(SECURITY_ATTRIBUTES);
		securityAttributes.bInheritHandle = TRUE;
		securityAttributes.lpSecurityDescriptor = NULL;

		// Create a pipe for the child process's STDOUT.
		if (!CreatePipe(&hChildStd_OUT_Rd, &hChildStd_OUT_Wr, &securityAttributes, 0))
		{
			Logf("StdoutRd CreatePipe error %d\n", GetLastError());
			free(commandLineString);
			return 1;
		}

		// Ensure the read handle to the pipe for STDOUT is not inherited.
		if (!SetHandleInformation(hChildStd_OUT_Rd, HANDLE_FLAG_INHERIT, 0))
		{
			Logf("Stdout SetHandleInformation error %d\n", GetLastError());
			free(commandLineString);
			CloseHandle(hChildStd_OUT_Rd);
			CloseHandle(hChildStd_OUT_Wr);
			return 1;
		}

		// Create a pipe for the child process's STDIN.
		if (!CreatePipe(&hChildStd_IN_Rd, &hChildStd_IN_Wr, &securityAttributes, 0))
		{
			Logf("Stdin CreatePipe error %d\n", GetLastError());
			free(commandLineString);
			CloseHandle(hChildStd_OUT_Rd);
			CloseHandle(hChildStd_OUT_Wr);
			return 1;
		}

		// Ensure the write handle to the pipe for STDIN is not inherited.
		if (!SetHandleInformation(hChildStd_IN_Wr, HANDLE_FLAG_INHERIT, 0))
		{
			Logf("Stdin SetHandleInformation error %d\n", GetLastError());
			free(commandLineString);
			CloseHandle(hChildStd_OUT_Rd);
			CloseHandle(hChildStd_OUT_Wr);
			CloseHandle(hChildStd_IN_Rd);
			CloseHandle(hChildStd_IN_Wr);
			return 1;
		}
	}

	STARTUPINFO startupInfo;
	ZeroMemory(&startupInfo, sizeof(startupInfo));
	startupInfo.cb = sizeof(startupInfo);
	// This structure specifies the STDIN and STDOUT handles for redirection.
	startupInfo.hStdError = hChildStd_OUT_Wr;
	startupInfo.hStdOutput = hChildStd_OUT_Wr;
	startupInfo.hStdInput = hChildStd_IN_Rd;
	startupInfo.dwFlags |= STARTF_USESTDHANDLES;

	PROCESS_INFORMATION* processInfo = new PROCESS_INFORMATION;
	ZeroMemory(processInfo, sizeof(PROCESS_INFORMATION));

	// Start the child process.
	if (!CreateProcess(fileToExecute,
	                   commandLineString,           // Command line
	                   nullptr,                     // No security attributes
	                   nullptr,                     // Thread handle not inheritable
	                   true,                        // Set handle inheritance to true
	                   0,                           // No creation flags
	                   nullptr,                     // Use parent's environment block
	                   arguments.workingDirectory,  // If nullptr, use parent's starting directory
	                   &startupInfo,                // Pointer to STARTUPINFO structure
	                   processInfo))                // Pointer to PROCESS_INFORMATION structure
	{
		CloseHandle(hChildStd_OUT_Rd);
		CloseHandle(hChildStd_OUT_Wr);
		CloseHandle(hChildStd_IN_Rd);
		CloseHandle(hChildStd_IN_Wr);
		free(commandLineString);
		int errorCode = GetLastError();
		if (errorCode == ERROR_FILE_NOT_FOUND)
		{
			Logf("CreateProcess failed to find file: %s\n", fileToExecute);
		}
		else if (errorCode == ERROR_PATH_NOT_FOUND)
		{
			Logf("CreateProcess failed to find path: %s\n", fileToExecute);
		}
		else
		{
			Logf("CreateProcess failed: %d\n", errorCode);
		}
		// LogLastError();
		return 1;
	}

	// Close handles to the stdin and stdout pipes no longer needed by the child process.
	// If they are not explicitly closed, there is no way to recognize that the child process ended
	CloseHandle(hChildStd_OUT_Wr);
	CloseHandle(hChildStd_IN_Rd);
	CloseHandle(hChildStd_IN_Wr);

	Subprocess newProcess = {0};
	newProcess.statusOut = statusOut;
	newProcess.processInfo = processInfo;
	newProcess.hChildStd_OUT_Rd = hChildStd_OUT_Rd;
	newProcess.command = commandLineString;
	s_subprocesses.push_back(std::move(newProcess));

	free(commandLineString);

	return 0;
#endif
	return 1;
}

#ifdef WINDOWS
void readProcessPipe(Subprocess& process, SubprocessOnOutputFunc onOutput)
{
	HANDLE hParentStdOut = GetStdHandle(STD_OUTPUT_HANDLE);

	char buffer[4096] = {0};
	bool encounteredError = false;
	while (true)
	{
		DWORD bytesRead = 0;
		DWORD bytesWritten = 0;
		bool success = ReadFile(process.hChildStd_OUT_Rd, buffer, sizeof(buffer) - 1, &bytesRead, NULL);
		if (!success || bytesRead == 0)
		{
			encounteredError = !success;
			break;
		}

		success = WriteFile(hParentStdOut, buffer, bytesRead, &bytesWritten, NULL);
		if (!success)
		{
			encounteredError = true;
			break;
		}

		if (onOutput)
		{
			buffer[bytesRead] = 0;
			onOutput(buffer);
		}
	}

	// This seems to give a lot of false-positives
	// if (encounteredError)
	// 	Log("warning: encountered read or write error while receiving sub-process "
	// 	    "output\n");
}
#endif

// This function prints all the output for each process in one contiguous block, so that outputs
// between two processes aren't mangled together terribly
void waitForAllProcessesClosed(SubprocessOnOutputFunc onOutput)
{
	if (s_subprocesses.empty())
		return;

	for (size_t i = 0; i < s_subprocesses.size(); ++i)
	{
		Subprocess* process = &s_subprocesses[i];
#if defined(UNIX) || defined(MACOS)
		char processOutputBuffer[1024] = {0};
		int numBytesRead =
		    read(process->pipeReadFileDescriptor, processOutputBuffer, sizeof(processOutputBuffer));
		while (numBytesRead > 0)
		{
			processOutputBuffer[numBytesRead] = '\0';
			subprocessReceiveStdOut(processOutputBuffer);
			if (onOutput)
				onOutput(processOutputBuffer);
			numBytesRead = read(process->pipeReadFileDescriptor, processOutputBuffer,
			                    sizeof(processOutputBuffer));
		}

		close(process->pipeReadFileDescriptor);

		waitpid(process->processId, process->statusOut, 0);

		// It's pretty useful to see the command which resulted in failure
		if (*process->statusOut != 0)
			Logf("%s\n", process->command.c_str());
#elif WINDOWS

		// We cannot wait indefinitely because the process eventually waits for us to read from the
		// output pipe (e.g. its buffer gets full). pollProcessTimeMilliseconds may need to be
		// tweaked for better performance; if the buffer is full, the subprocess will wait for as
		// long as pollProcessTimeMilliseconds - time taken to fill buffer. Very low wait times will
		// mean Cakelisp unnecessarily taking up cycles, so it's a tradeoff.
		const int pollProcessTimeMilliseconds = 50;
		while (WAIT_TIMEOUT ==
		       WaitForSingleObject(process->processInfo->hProcess, pollProcessTimeMilliseconds))
			readProcessPipe(*process, onOutput);

		// If the wait was ended but wasn't a timeout, we still need to read out
		readProcessPipe(*process, onOutput);

		DWORD exitCode = 0;
		if (!GetExitCodeProcess(process->processInfo->hProcess, &exitCode))
		{
			Log("error: failed to get exit code for process\n");
			exitCode = 1;
		}
		else if (exitCode != 0)
		{
			Logf("%s\n", process->command.c_str());
		}

		*(process->statusOut) = exitCode;

		// Close process, thread, and stdout handles.
		CloseHandle(process->processInfo->hProcess);
		CloseHandle(process->processInfo->hThread);
		CloseHandle(process->hChildStd_OUT_Rd);
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

static const char* ProcessCommandArgumentTypeToString(ProcessCommandArgumentType type)
{
	switch (type)
	{
		case ProcessCommandArgumentType_None:
			return "None";
		case ProcessCommandArgumentType_String:
			return "String";
		case ProcessCommandArgumentType_SourceInput:
			return "SourceInput";
		case ProcessCommandArgumentType_ObjectOutput:
			return "ObjectOutput";
		case ProcessCommandArgumentType_DebugSymbolsOutput:
			return "DebugSymbolsOutput";
		case ProcessCommandArgumentType_ImportLibraryPaths:
			return "ImportLibraryPaths";
		case ProcessCommandArgumentType_ImportLibraries:
			return "ImportLibraries";
		case ProcessCommandArgumentType_CakelispHeadersInclude:
			return "CakelispHeadersInclude";
		case ProcessCommandArgumentType_IncludeSearchDirs:
			return "IncludeSearchDirs";
		case ProcessCommandArgumentType_AdditionalOptions:
			return "AdditionalOptions";
		case ProcessCommandArgumentType_PrecompiledHeaderOutput:
			return "PrecompiledHeaderOutput";
		case ProcessCommandArgumentType_PrecompiledHeaderInclude:
			return "PrecompiledHeaderInclude";
		case ProcessCommandArgumentType_ObjectInput:
			return "ObjectInput";
		case ProcessCommandArgumentType_DynamicLibraryOutput:
			return "DynamicLibraryOutput";
		case ProcessCommandArgumentType_LibrarySearchDirs:
			return "LibrarySearchDirs";
		case ProcessCommandArgumentType_Libraries:
			return "Libraries";
		case ProcessCommandArgumentType_LibraryRuntimeSearchDirs:
			return "LibraryRuntimeSearchDirs";
		case ProcessCommandArgumentType_LinkerArguments:
			return "LinkerArguments";
		case ProcessCommandArgumentType_ExecutableOutput:
			return "ExecutableOutput";
		default:
			return "Unknown";
	}
}

// The array will need to be deleted, but the array members will not
const char** MakeProcessArgumentsFromCommand(const char* fileToExecute,
                                             std::vector<ProcessCommandArgument>& arguments,
                                             const ProcessCommandInput* inputs, int numInputs)
{
	std::vector<const char*> argumentsAccumulate;

	for (unsigned int i = 0; i < arguments.size(); ++i)
	{
		ProcessCommandArgument& argument = arguments[i];

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
					{
						if (!value || !value[0])
						{
							Logf(
							    "warning: attempted to pass null string to '%s' under argument "
							    "type %s. It will be ignored\n",
							    fileToExecute, ProcessCommandArgumentTypeToString(argument.type));
							continue;
						}

						argumentsAccumulate.push_back(value);
					}
					found = true;
					break;
				}
			}
			if (!found)
			{
				Logf("error: command to %s missing ProcessCommandInput of type %s\n", fileToExecute,
				     ProcessCommandArgumentTypeToString(argument.type));
				return nullptr;
			}
		}
	}

	int numUserArguments = argumentsAccumulate.size();
	// +1 for file to execute
	int numFinalArguments = numUserArguments + 1;
	// +1 again for the null terminator
	const char** newArguments = (const char**)calloc(sizeof(const char*), numFinalArguments + 1);

	newArguments[0] = fileToExecute;
	for (int i = 1; i < numFinalArguments; ++i)
		newArguments[i] = argumentsAccumulate[i - 1];
	newArguments[numFinalArguments] = nullptr;

	return newArguments;
}

#include <thread>
const int maxProcessesRecommendedSpawned = std::thread::hardware_concurrency();
