#pragma once

enum StringOutputModifierFlags
{
	StringOutMod_None = 0,
	StringOutMod_NewlineAfter = 1 << 0,
	StringOutMod_SpaceAfter = 1 << 1,
	StringOutMod_SpaceBefore = 1 << 2,

	// There needs to be enough information to know what a thing is to pick the format, which is why
	// this list is rather short
	StringOutMod_ConvertTypeName = 1 << 3,
	StringOutMod_ConvertFunctionName = 1 << 4,
	StringOutMod_ConvertVariableName = 1 << 5,

	StringOutMod_SurroundWithQuotes = 1 << 6,

	// Curly braces ({}) will be added for open and close, respectively
	// These are also clues to the output formatter to indent all within, etc.
	StringOutMod_OpenBlock = 1 << 7,
	StringOutMod_CloseBlock = 1 << 8,

	StringOutMod_OpenParen = 1 << 9,
	StringOutMod_CloseParen = 1 << 10,

	// In C, ';' with a new line
	StringOutMod_EndStatement = 1 << 11,

	// ',', for lists of arguments, expressions (e.g. initializer lists), etc.
	StringOutMod_ListSeparator = 1 << 12,

	// Uses {} for initializer lists etc.
	StringOutMod_OpenList = 1 << 13,
	StringOutMod_CloseList = 1 << 14,

	// Signals the Writer that it needs to splice in another output list
	StringOutMod_Splice = 1 << 15,
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

enum ObjectType
{
	ObjectType_Function,
	ObjectType_CompileTimeMacro,
	ObjectType_CompileTimeGenerator,
};

enum ObjectReferenceGuessState
{
	GuessState_None = 0,
	GuessState_Guessed,
	// References can skip the guessed state if the definition is already known but not loaded
	GuessState_WaitingForLoad,
	GuessState_Resolved,
};

enum ProcessCommandArgumentType
{
	ProcessCommandArgumentType_String,
	ProcessCommandArgumentType_SourceInput,
	ProcessCommandArgumentType_ObjectOutput,
	ProcessCommandArgumentType_CakelispHeadersInclude,
};
