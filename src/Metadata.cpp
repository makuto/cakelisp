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
	EvaluationTime_EvaluatedOnImport = 1 << 3,
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
	GeneratorCategory_Definitions,
	GeneratorCategory_CodeGeneration,
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
     "[DEPRECATED] Mark the current module to be excluded from the runtime build process. This is "
     "necessary when the module does not provide any runtime code. For example, a module with only "
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
    {"add-static-link-objects", GeneratorCategory_Build, LanguageRequirement_Evaluated,
     EvaluationTime_EvaluatedImmediately, 0, MaxArgumentsUnlimited,
     "Link additional objects, static libraries, or (on Windows) compiled resources. Modification "
     "times of the files in this list will be checked and cause a re-link if they are newer than "
     "the cached executable."},
    {
        "add-compiler-link-options",
    },
    {
        "add-build-config-label",
    },
    {
        "add-library-dependency",
    },

    {
        "import",
    },
    {
        "c-import",
    },

    {
        "comptime-define-symbol",
    },
    {
        "comptime-cond",
    },
    {
        "ignore",
    },
    {"export", GeneratorCategory_Uncategorized, LanguageRequirement_Evaluated,
     EvaluationTime_EvaluatedOnImport, 1, MaxArgumentsUnlimited,
     "When any other module imports the current module, evaluate the statements within this export "
     "scope in the context of the other module. This allows modules to e.g. 'infect' other modules "
     "with settings necessary for the importer to build. Compare to (export-and-evaluate), which "
     "evaluates both in the export-defining module and in importer contexts."},
    {"export-and-evaluate", GeneratorCategory_Uncategorized, LanguageRequirement_Evaluated,
     (EvaluationTime_EvaluatedImmediately | EvaluationTime_EvaluatedOnImport), 1,
     MaxArgumentsUnlimited,
     "Evaluate the contained statements. Additionally, when any other module imports the current "
     "module, evaluate the statements within this export scope in the context of the other module. "
     "This allows modules to e.g. 'infect' other modules with settings necessary for the importer "
     "to build. Compare to (export), which only evaluates in importer contexts."},

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
        "bit-or",
    },
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
    {">=", GeneratorCategory_Relational, LanguageRequirement_C,
     (EvaluationTime_CompileTime | EvaluationTime_Runtime), 2, 2,
     "Evaluate if the first argument is greater than or equal to the second argument."},
    {"<=", GeneratorCategory_Relational, LanguageRequirement_C,
     (EvaluationTime_CompileTime | EvaluationTime_Runtime), 2, 2,
     "Evaluate if the first argument is less than or equal to the second argument."},
    {">", GeneratorCategory_Relational, LanguageRequirement_C,
     (EvaluationTime_CompileTime | EvaluationTime_Runtime), 2, 2,
     "Evaluate if the first argument is greater than the second argument."},
    {"<", GeneratorCategory_Relational, LanguageRequirement_C,
     (EvaluationTime_CompileTime | EvaluationTime_Runtime), 2, 2,
     "Evaluate if the first argument is less than the second argument."},

    //
    // Logical
    //
    {"and", GeneratorCategory_Logic, LanguageRequirement_C,
     (EvaluationTime_CompileTime | EvaluationTime_Runtime), 2, MaxArgumentsUnlimited,
     "Evaluate to true if all the expression arguments result in true. Lazily evaluates, "
     "i.e. evaluation will stop as soon as any expression evaluates to false."},
    {"or", GeneratorCategory_Logic, LanguageRequirement_C,
     (EvaluationTime_CompileTime | EvaluationTime_Runtime), 2, MaxArgumentsUnlimited,
     "Evaluate to true if any of the expression arguments result in true. Lazily evaluates, "
     "i.e. evaluation will stop as soon as any expression evaluates to true."},
    {"not", GeneratorCategory_Logic, LanguageRequirement_C,
     (EvaluationTime_CompileTime | EvaluationTime_Runtime), 1, 1,
     "Invert the boolean result of the argument. E.g. (not true) = false, (not false) = true"},

    //
    // Control flow
    //
    {"return", GeneratorCategory_ControlFlow, LanguageRequirement_C,
     (EvaluationTime_CompileTime | EvaluationTime_Runtime), 0, 1,
     "Return from the current function. If a return value is specified, return that value to the "
     "caller."},
    {"if", GeneratorCategory_ControlFlow, LanguageRequirement_C,
     (EvaluationTime_CompileTime | EvaluationTime_Runtime), 2, 3,
     "If the first argument (condition) evaluates to true, execute the second argument (true "
     "block). If the first argument evaluates to false, execute the third argument (false block), "
     "if specified. Use (scope) in order to specify more than one statement in the true or false "
     "blocks."},
    {"when", GeneratorCategory_ControlFlow, LanguageRequirement_C,
     (EvaluationTime_CompileTime | EvaluationTime_Runtime), 2, 2,
     "If the first argument (condition) evaluates to true, execute the second argument (true "
     "block). This is a way to write e.g. (if condition (do-thing))"},
    {"unless", GeneratorCategory_ControlFlow, LanguageRequirement_C,
     (EvaluationTime_CompileTime | EvaluationTime_Runtime), 2, 2,
     "If the first argument (condition) evaluates to false, execute the second argument (false "
     "block). This is a cleaner way to write e.g. (if (not condition) (do-thing))"},
    {
        "cond",
    },
    {
        "break",
    },
    {
        "while",
    },
    {
        "?",
    },
    {
        "for-in",
    },
    {
        "continue",
    },

    {
        "type-cast",
    },
    {
        "call-on",
    },
    {
        "scope",
    },
    {
        "rename-builtin",
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
        "comptime-error",
    },
    {"defer", GeneratorCategory_ControlFlow, LanguageRequirement_C,
     (EvaluationTime_CompileTime | EvaluationTime_Runtime), 1, MaxArgumentsUnlimited,
     "Defer the statements in the body until the current scope is exited. This is useful to e.g. "
     "(defer (free buffer)). defer will ensure that the code is called, both in \"natural\" scope "
     "exits and \"explicit\" exits (those caused by return, continue, or break)."},

    //
    // Definitions
    //
    {"var", GeneratorCategory_Definitions, LanguageRequirement_C,
     (EvaluationTime_CompileTime | EvaluationTime_Runtime), 2, 3,
     "Define a variable. The first argument is the name, the second is the type, and the optional "
     "third argument is the initial value. When used in module scope (i.e., not within any "
     "function), the variable will be local to the module and initialized on application startup."},
    {"var-global", GeneratorCategory_Definitions, LanguageRequirement_C,
     (EvaluationTime_CompileTime | EvaluationTime_Runtime), 2, 3,
     "Define a global variable. The first argument is the name, the second is the type, and the "
     "optional third argument is the initial value. This is only valid in module scope (i.e., not "
     "within any function). The variable will be exposed to other modules which import the current "
     "module. It is initialized on application startup."},
    {"var-static", GeneratorCategory_Definitions, LanguageRequirement_C,
     (EvaluationTime_CompileTime | EvaluationTime_Runtime), 2, 3,
     "Define a static variable. The first argument is the name, the second is the type, and the "
     "optional third argument is the initial value. This is only valid in body scope (i.e., within "
     "a function). The variable will retain its value since the previous execution of the "
     "function. It is initialized the first time the declaration is hit at runtime."},
    {
        "defgenerator",
    },
    {
        "defmacro",
    },
    {
        "defstruct",
    },
    {
        "defstruct-local",
    },
    {
        "def-function-signature",
    },
    {
        "def-function-signature-global",
    },
    {
        "defun",
    },
    {
        "defun-local",
    },
    {
        "defun-nodecl",
    },
    {
        "defun-comptime",
    },
    {
        "def-type-alias",
    },
    {
        "def-type-alias-global",
    },
    {
        "array",
    },

    //
    // Memory
    //
    {
        "set",
    },
    {"field", GeneratorCategory_Memory, LanguageRequirement_C,
     (EvaluationTime_CompileTime | EvaluationTime_Runtime), 2, MaxArgumentsUnlimited,
     "field accesses the field of a structure. (field my-struct field) would generate "
     "myStruct.field. If you need to access fields through a pointer, use (path) instead."},
    {"path", GeneratorCategory_Memory, LanguageRequirement_C,
     (EvaluationTime_CompileTime | EvaluationTime_Runtime), 3, MaxArgumentsUnlimited,
     "path allows you to access nested fields, including through pointers. For example, (path "
     "my-ptr > ptr-to-struct > inline-struct . field) would generate "
     "myPtr->ptrToStruct->inlineStruct.field. Use (field) if you are not accessing memory "
     "through pointers."},
    {
        "addr",
    },
    {
        "deref",
    },
    {
        "at",
    },

    //
    // Code generation
    //
    {
        "tokenize-push",
        GeneratorCategory_CodeGeneration,
    },

    {"splice-point", GeneratorCategory_CodeGeneration, LanguageRequirement_Evaluated,
     EvaluationTime_EvaluatedImmediately, 1, 1,
     "Define a new named splice point. This point can be used at compile-time by functions like "
     "ClearAndEvaluateAtSplicePoint() to evaluate code at specific locations in the module. A "
     "similar function is ReplaceAndEvaluateDefinition(), which is required when replacing a "
     "definition in order to notify the Environment to remove the old definition."},

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
    // C++ helpers. TODO: Move to CppHelpers.cake instead?
    //
    {
        "in",
    },
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
