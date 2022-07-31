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
	// If you expect statements to run within, use StringOutMod_OpenScopeBlock and
	// StringOutMod_CloseScopeBlock instead.
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

	// The Writer should paste the spliceOutput of this operation before exiting the current scope
	StringOutMod_SpliceOnScopeExit = 1 << 16,

	StringOutMod_ScopeEnter = 1 << 17,
	StringOutMod_ScopeExit = 1 << 18,
	// for/while/switch all can have continue or break, which "exits" the scope or maybe just
	// restarts it. We need to handle these specially because they can exist within another scope,
	// so the writer needs to peel back all scopes
	StringOutMod_ScopeContinueBreakableEnter = 1 << 19,
	StringOutMod_ScopeContinueBreakableExit = 1 << 20,
	StringOutMod_ScopeContinueOrBreak = 1 << 21,
	StringOutMod_ScopeExitAll = 1 << 22,

	StringOutMod_OpenScopeBlock = StringOutMod_OpenBlock | StringOutMod_ScopeEnter,
	StringOutMod_CloseScopeBlock = StringOutMod_CloseBlock | StringOutMod_ScopeExit,
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
	ObjectType_PseudoObject,
	ObjectType_Function,
	// Note: Does not include stack variables, i.e. variables in function bodies
	ObjectType_Variable,

	ObjectType_CompileTimeMacro,
	ObjectType_CompileTimeGenerator,
	ObjectType_CompileTimeFunction,
	// Built at compile time, but not hooked up as a regular generator. These are hacked in and
	// late-resolved. TODO Clean up...
	ObjectType_CompileTimeExternalGenerator,
};

enum ObjectReferenceGuessState
{
	GuessState_None = 0,
	GuessState_Guessed,
	// References can skip the guessed state if the definition is already known but not loaded
	GuessState_WaitingForLoad,
	GuessState_Resolved,
};

enum ObjectReferenceResolutionType
{
	ObjectReferenceResolutionType_None = 0,
	ObjectReferenceResolutionType_Splice,
	// In the case of compile-time functions, the first reference encountered was when the comptime
	// function was already loaded, so no action needs to be taken
	ObjectReferenceResolutionType_AlreadyLoaded
};

enum RequiredFeature
{
	// There's an assumption built-in to cakelisp that C should work, so that's the equivalent here
	RequiredFeature_None = 0,
	// Track separately so that C++ functions can be called from C unless they have C++ types
	RequiredFeature_CppInDefinition = 1 << 1,
	RequiredFeature_CppInDeclaration = 1 << 2,
	RequiredFeature_Cpp = (RequiredFeature_CppInDefinition | RequiredFeature_CppInDeclaration),
};
