#pragma once

enum TokenType
{
	TokenType_OpenParen,
	TokenType_CloseParen,
	// Note that Symbols include numerical constants
	TokenType_Symbol,
	TokenType_String,

	// Special type: This shouldn't ever be visible by anything but tokenizeLine, unless the input
	// has a string continuation error (for multi-line strings)
	// Merge: String is quoted on all lines, but backslash denotes merge with previous string
	// Continue: String is quoted at start and end, and newlines are ignored. No space is inserted
	// HereString: Everything from #"# to #"# will be literally copied into a string
	TokenType_StringMerge,
	TokenType_StringContinue,
	TokenType_HereString,
};
