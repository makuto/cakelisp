#pragma once

enum TokenType
{
	TokenType_OpenParen,
	TokenType_CloseParen,
	// Note that Symbols include numerical constants
	TokenType_Symbol,
	TokenType_String
};
