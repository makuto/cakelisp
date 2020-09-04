#pragma once

enum StringOutputModifierFlags
{
	StringOutMod_None = 0,
	StringOutMod_NewlineAfter = 1 << 0,
	StringOutMod_SpaceAfter = 1 << 1,

	StringOutMod_ConvertTypeName = 1 << 2,
	StringOutMod_ConvertFunctionName = 1 << 3,
	StringOutMod_ConvertArgumentName = 1 << 4,
	StringOutMod_ConvertVariableName = 1 << 5,
	StringOutMod_ConvertGlobalVariableName = 1 << 6,

	StringOutMod_SurroundWithQuotes = 1 << 7,

	// Curly braces ({}) will be added for open and close, respectively
	// These are also clues to the output formatter to indent all within, etc.
	StringOutMod_OpenBlock = 1 << 8,
	StringOutMod_CloseBlock = 1 << 9,

	StringOutMod_OpenParen = 1 << 10,
	StringOutMod_CloseParen = 1 << 11,

	// In C, ';' with a new line
	StringOutMod_EndStatement = 1 << 12,

	// ',', for lists of arguments, expressions (e.g. initializer lists), etc.
	StringOutMod_ListSeparator = 1 << 13,

	// Uses {} for initializer lists etc.
	StringOutMod_OpenList = 1 << 14,
	StringOutMod_CloseList = 1 << 15,
};

enum ImportLanguage
{
	ImportLanguage_None = 0,
	ImportLanguage_C,
	ImportLanguage_Cakelisp
};

// This allows generators to react and perform validation in different scopes, because few
// generators will work in any scope
enum EvaluatorScope
{
	// Anything that takes plain-old declarations
	EvaluatorScope_Body,
	// Top-level invocations in a file, for example
	EvaluatorScope_Module,
	// For example, a C function call cannot have an if statement in its arguments
	EvaluatorScope_ExpressionsOnly
};
