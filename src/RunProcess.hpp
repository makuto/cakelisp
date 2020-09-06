#pragma once

struct CompilationArguments
{
	const char* fileToExecute;
	char** arguments;
};

int compileFile(const CompilationArguments& arguments);
