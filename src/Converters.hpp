#pragma once

enum NameStyleMode
{
	NameStyleMode_None = 0,
	// a-test-thing -> a_test_thing
	NameStyleMode_Underscores,
	// a-test-thing -> aTestThing
	NameStyleMode_CamelCase,
	// a-test-thing -> ATestThing
	// Do NOT use for typeNameMode, because it will destroy C types e.g. int will become Int
	NameStyleMode_PascalCase,
	// Only upcase the first letter if the string looks to be a lisp-style plural type:
	// a-test-thing -> ATestThing
	// mytype -> mytype
	// This is gross, but I can't think of a good way to allow C-style types inline with Cakelisp
	// types without knowing which is which, e.g.:
	//  my-cake-type arg1, int arg2 -> MyCakeType arg1, int arg2
	// caketype arg1, int arg2 -> caketype arg1, int arg2
	NameStyleMode_PascalCaseIfPlural
};

struct NameStyleSettings
{
	// It is unwise to change this to pascal case because it will destroy C types which start with
	// lowercase characters (e.g. "int")
	NameStyleMode typeNameMode = NameStyleMode_PascalCaseIfPlural;
	NameStyleMode functionNameMode = NameStyleMode_CamelCase;
	NameStyleMode argumentNameMode = NameStyleMode_CamelCase;
	NameStyleMode variableNameMode = NameStyleMode_CamelCase;
	NameStyleMode globalVariableNameMode = NameStyleMode_CamelCase;
};

// This only does anything to strings which have '-' as separators, or other symbols not allowed in
// C names. It's safe to e.g. pass in valid C names because they cannot have lisp-allowed symbols in
// them. This also means you can use whatever style you want in Cakelisp, and you'll get valid C/C++
// generated (so long as your non-'-' strings match the other C/C++ names)
void lispNameStyleToCNameStyle(NameStyleMode mode, const char* name, char* bufferOut,
                               int bufferOutSize);
