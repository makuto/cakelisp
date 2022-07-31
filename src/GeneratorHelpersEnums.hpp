#pragma once

enum CStatementOperationType
{
	// Insert keywordOrSymbol between each thing
	Splice,
	SpliceNoSpace,

	OpenParen,
	CloseParen,

	// Open a scope. Always use when regular statements can be executed within
	OpenScope,
	CloseScope,
	// Anything which responds to continue or break should open its scope with these
	OpenContinueBreakableScope,
	CloseContinueBreakableScope,
	// Do not open a scope. Use only for declarations
	OpenBlock,
	CloseBlock,
	// Used only for e.g. array initializers
	OpenList,
	CloseList,

	// "Unnatural" flow control, i.e., cases where the normal CloseScope will not be hit
	ContinueOrBreakInScope,
	// return keyword
	ExitAllScopes,

	Keyword,
	KeywordNoSpace,
	// End the statement if it isn't an expression
	SmartEndStatement,

	// Arrays require appending the [] bit, which is too complicated for CStatementOperations
	TypeNoArray,

	// Evaluate argument(s)
	Expression,
	ExpressionOptional,
	ExpressionList,
	// Body will read the remaining arguments; argumentIndex will tell it where to start
	Body,
	// Similar to body, though doesn't evaluate more than one argument. Users can still use
	// invocations which open scopes in order to provide multiple statements in a single Statement.
	// Statements are in Body scope
	Statement,
};

enum TokenizePushSpliceArgumentType
{
	TokenizePushSpliceArgument_Array,
	TokenizePushSpliceArgument_AllExpressions,
	TokenizePushSpliceArgument_Expression
};
