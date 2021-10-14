#include <string>
// #include <unordered_map>

#include "Evaluator.hpp"
#include "Generators.hpp"
#include "Utilities.hpp"

enum LanguageRequirement
{
	// Cakelisp's Evaluator will handle the invocation without generating any C/C++ code
	LanguageRequirement_Evaluated,

	// All C features are considered valid C++
	LanguageRequirement_C,

	// Only features which only work in C++ and do not work in C
	LanguageRequirement_Cpp,
};

enum EvaluationTime
{
	EvaluationTime_EvaluatedImmediately = 1 << 0,
	EvaluationTime_CompileTime = 1 << 1,
	EvaluationTime_Runtime = 1 << 2,
};

enum GeneratorCategory
{
	GeneratorCategory_Uncategorized,
	GeneratorCategory_Math,
	GeneratorCategory_Logic,
	GeneratorCategory_Relational,
	GeneratorCategory_Build,
	GeneratorCategory_ControlFlow,
	GeneratorCategory_Memory,
};

struct GeneratorMetadata
{
	const char* generatorName;
	GeneratorCategory category;
	LanguageRequirement language;
	int evaluationTime; // EvaluationTime flags
	int minimumArguments;
	int maximumArguments;
	const char* description;
};

const int MaxArgumentsUnlimited = 99;

GeneratorMetadata g_generatorMetadata[] = {
    //
    // Build
    //
    {"skip-build", GeneratorCategory_Build, LanguageRequirement_Evaluated,
     EvaluationTime_EvaluatedImmediately, 0, 0,
     "Mark the current module to be excluded from the runtime build process. This is necessary "
     "when the module does not provide any runtime code. For example, a module with only "
     "compile-time function definitions would need to skip building."},
    {
        "add-c-search-directory-global",
    },
    {
        "add-cakelisp-search-directory",
    },
    {
        "add-library-search-directory",
    },
    {
        "add-c-search-directory-module",
    },
    {
        "add-library-runtime-search-directory",
    },
    {
        "set-module-option",
    },
    {
        "set-cakelisp-option",
    },
    {
        "add-build-options-global",
    },
	{
        "add-cpp-build-dependency",
    },
    {
        "add-c-build-dependency",
    },
    {
        "add-build-options",
    },
    {
        "add-compile-time-hook",
    },
    {
        "add-compile-time-hook-module",
    },
	{
        "add-linker-options",
    },

    //
    // Math
    //
    {"decr", GeneratorCategory_Math, LanguageRequirement_C,
     (EvaluationTime_CompileTime | EvaluationTime_Runtime), 1, 1,
     "Subtract one from the first argument and set it to the new value. The subtraction is a "
     "pre-decrement. Equivalent to (set arg (- arg 1))"},
    {"--", GeneratorCategory_Math, LanguageRequirement_C,
     (EvaluationTime_CompileTime | EvaluationTime_Runtime), 1, 1,
     "Subtract one from the first argument and set it to the new value. The subtraction is a "
     "pre-decrement. Equivalent to (set arg (- arg 1))"},
    {"incr", GeneratorCategory_Math, LanguageRequirement_C,
     (EvaluationTime_CompileTime | EvaluationTime_Runtime), 1, 1,
     "Add one to the first argument and set it to the new value. The addition is a pre-increment. "
     "Equivalent to (set arg (+ arg 1))"},
    {"++", GeneratorCategory_Math, LanguageRequirement_C,
     (EvaluationTime_CompileTime | EvaluationTime_Runtime), 1, 1,
     "Add one to the first argument and set it to the new value. The addition is a pre-increment. "
     "Equivalent to (set arg (+ arg 1))"},
    {"mod", GeneratorCategory_Math, LanguageRequirement_C,
     (EvaluationTime_CompileTime | EvaluationTime_Runtime), 2, 2,
     "Perform the modulo (%) operation on the first value with the second as the divisor. E.g. "
     "(mod 5 2) = 1. The modulo operator returns the remainder after the division."},
    {"%", GeneratorCategory_Math, LanguageRequirement_C,
     (EvaluationTime_CompileTime | EvaluationTime_Runtime), 2, 2,
     "Perform the modulo (%) operation on the first value with the second as the divisor. E.g. (% "
     "5 2) = 1. The modulo operator returns the remainder after the division."},
    {"/", GeneratorCategory_Math, LanguageRequirement_C,
     (EvaluationTime_CompileTime | EvaluationTime_Runtime), 2, MaxArgumentsUnlimited,
     "Divide the arguments. E.g. (/ 4 2) = 2."},
    {"*", GeneratorCategory_Math, LanguageRequirement_C,
     (EvaluationTime_CompileTime | EvaluationTime_Runtime), 2, MaxArgumentsUnlimited,
     "Multiply the arguments. E.g. (* 4 2) = 8."},
    {"+", GeneratorCategory_Math, LanguageRequirement_C,
     (EvaluationTime_CompileTime | EvaluationTime_Runtime), 2, MaxArgumentsUnlimited,
     "Add the arguments. E.g. (+ 4 2) = 6."},
    {"-", GeneratorCategory_Math, LanguageRequirement_C,
     (EvaluationTime_CompileTime | EvaluationTime_Runtime), 2, MaxArgumentsUnlimited,
     "Subtract the arguments. E.g. (- 4 2) = 2."},
    {"bit-<<", GeneratorCategory_Math, LanguageRequirement_C,
     (EvaluationTime_CompileTime | EvaluationTime_Runtime), 2, 2,
     "Shift the bits in the first argument to the left by the number indicated by the second "
     "argument. E.g. (bit-<< 1 2) = 4."},
    {"bit->>", GeneratorCategory_Math, LanguageRequirement_C,
     (EvaluationTime_CompileTime | EvaluationTime_Runtime), 2, 2,
     "Shift the bits in the first argument to the right by the number indicated by the second "
     "argument. E.g. (bit->> 4 2) = 1."},
    {
        "bit-ones-complement",
    },
    {
        "bit-xor",
    },
    {
        "bit-and",
    },

    //
    // Relational
    //
    {"=", GeneratorCategory_Relational, LanguageRequirement_C,
     (EvaluationTime_CompileTime | EvaluationTime_Runtime), 2, 2,
     "Evaluate if the two arguments are equal. Evaluates as true if equal or false otherwise."},
    {"!=", GeneratorCategory_Relational, LanguageRequirement_C,
     (EvaluationTime_CompileTime | EvaluationTime_Runtime), 2, 2,
     "Evaluate if the two arguments are not equal. Evaluates as true if not equal or false if "
     "equal."},
    {
        ">=",
    },
    {
        "<=",
    },
    {
        ">",
    },
    {
        "<",
    },

    //
    // Logical
    //
    {
        "and",
    },
    {
        "or",
    },

    //
    // Control flow
    //
    {"return", GeneratorCategory_ControlFlow, LanguageRequirement_C,
     (EvaluationTime_CompileTime | EvaluationTime_Runtime), 0, 1,
     "Return from the current function. If a return value is specified, return that value to the "
     "caller."},
    {
        "tokenize-push",
    },
    {
        "if",
    },
    {
        "var-static",
    },
    {
        "at",
    },
    {
        "defmacro",
    },
    {
        "nth",
    },
    {
        "not",
    },
    {
        "type-cast",
    },
    {
        "path",
    },
    {
        "var-global",
    },
    {
        "defun-local",
    },
    {
        "bit-or",
    },
    {
        "import",
    },
    {
        "var",
    },
    {
        "in",
    },
    {
        "break",
    },
    {
        "c-import",
    },
    {
        "defun",
    },
    {
        "add-library-dependency",
    },
    {
        "defstruct-local",
    },
    {
        "defun-nodecl",
    },
    {
        "add-compiler-link-options",
    },
    {
        "add-build-config-label",
    },
    {
        "cond",
    },
    {
        "defun-comptime",
    },
    {
        "while",
    },
    {
        "def-function-signature",
    },
    {
        "defgenerator",
    },
    {
        "?",
    },
    {
        "call-on",
    },
    {
        "when",
    },
    {
        "def-type-alias-global",
    },
    {
        "def-function-signature-global",
    },
    {
        "scope",
    },
    {
        "rename-builtin",
    },
    {
        "comptime-cond",
    },
    {
        "ignore",
    },
    {
        "comptime-define-symbol",
    },
    {
        "def-type-alias",
    },
    {
        "array",
    },
    {
        "set",
    },
    {
        "for-in",
    },
    {
        "continue",
    },
    {
        "unless",
    },

	//
	// Definitions
	//
	{
        "defstruct",
    },

    {
        "block",
    },
    {
        "type",
    },
    {
        "call",
    },
    {
        "call-on-ptr",
    },
    {
        "field",
    },
    {
        "deref",
    },
    {
        "addr",
    },

	{
        "comptime-error",
    },

    //
    // Misc. C helpers
    //
    {
        "c-preprocessor-define",
    },
    {
        "c-preprocessor-define-global",
    },

    //
    // C++ helpers. TODO: Move to CppHelpers.cake instead
    //
    {"new", GeneratorCategory_Memory, LanguageRequirement_Cpp,
     (EvaluationTime_CompileTime | EvaluationTime_Runtime), 1, 1,
     "Run new on the first argument, a type. Returns a pointer to the memory allocated after the "
     "constructor has been called."},
    {"delete", GeneratorCategory_Memory, LanguageRequirement_Cpp,
     (EvaluationTime_CompileTime | EvaluationTime_Runtime), 1, 1,
     "Run delete on the argument, calling its destructor then freeing the memory."},
    {"new-array", GeneratorCategory_Memory, LanguageRequirement_Cpp,
     (EvaluationTime_CompileTime | EvaluationTime_Runtime), 2, 2,
     "Run new[] on the first argument, a type. The second argument is the number of instances to "
     "create. Returns a pointer to the memory allocated after the constructor has been called for "
     "each instance."},
    {"delete-array", GeneratorCategory_Memory, LanguageRequirement_Cpp,
     (EvaluationTime_CompileTime | EvaluationTime_Runtime), 1, 1,
     "Run delete[] on the argument, which should be a pointer to an array created via new[]. Each "
     "instance will have its destructor called, then the memory will be freed."},
};

void printBuiltInGeneratorMetadata(EvaluatorEnvironment* environment)
{
	for (GeneratorIterator it = environment->generators.begin();
	     it != environment->generators.end(); ++it)
	{
		GeneratorMetadata* foundMetadata = nullptr;
		for (unsigned int i = 0; i < ArraySize(g_generatorMetadata); ++i)
		{
			if (0 == it->first.compare(g_generatorMetadata[i].generatorName))
			{
				foundMetadata = &g_generatorMetadata[i];
				break;
			}
		}

		if (!foundMetadata)
			Logf("warning: built-in generator %s is missing metadata\n", it->first.c_str());
	}

	for (unsigned int i = 0; i < ArraySize(g_generatorMetadata); ++i)
	{
		GeneratorMetadata* currentMetadata = &g_generatorMetadata[i];
		Logf("------------------------------\n%s\n\t%s\n", currentMetadata->generatorName,
		     currentMetadata->description);
	}

	// TODO: Also check that nothing in the metadata is missing from the generators list.
}
