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
	NameStyleMode_PascalCaseIfLispy
};
