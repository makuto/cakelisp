#pragma once

#include "EvaluatorEnums.hpp"
#include "RunProcess.hpp"

#include <string>
#include <vector>
// TODO: Replace with fast hash table
#include <unordered_map>

struct GeneratorOutput;
struct ModuleManager;
struct Module;
struct NameStyleSettings;
struct Token;

// Rather than needing to allocate and edit a buffer eventually equal to the size of the final
// output, store output operations instead. This also facilitates source <-> generated mapping data
struct StringOutput
{
	// TODO: Putting this in a union means we need to write a destructor which can detect when to
	// destroy output
	//union {
	std::string output;
	GeneratorOutput* spliceOutput;
	// };

	StringOutputModifierFlags modifiers;

	// Used to correlate Cakelisp code with generated output code
	const Token* startToken;
};

struct FunctionArgumentMetadata
{
	std::string name;

	const Token* typeStartToken;
	// Unnecessary because we can just keep track of our parens, but included for convenience
	const Token* typeEndToken;
};

struct FunctionMetadata
{
	// The Cakelisp name, NOT the converted C name
	std::string name;

	// From the opening paren of e.g. "(defun" to the final ")" closing the body
	const Token* startDefinitionToken;
	// Unnecessary because we can just keep track of our parens, but included for convenience
	const Token* endDefinitionToken;

	std::vector<FunctionArgumentMetadata> arguments;
};

const char* importLanguageToString(ImportLanguage type);

struct ImportMetadata
{
	std::string importName;
	ImportLanguage language;
	const Token* triggerToken;
};

struct GeneratorOutput
{
	std::vector<StringOutput> source;
	std::vector<StringOutput> header;

	std::vector<FunctionMetadata> functions;
	std::vector<ImportMetadata> imports;
};
// Add members to this as necessary
void resetGeneratorOutput(GeneratorOutput& output);

// This is frequently copied, so keep it small
struct EvaluatorContext
{
	EvaluatorScope scope;
	// Macro and generator definitions need to be resolved first
	bool isMacroOrGeneratorDefinition;

	// Whether or not the code in this context is required to be generated, i.e. isn't "dead" code
	bool isRequired;
	// Associate all unknown references with this definition
	const Token* definitionName;
	Module* module;
};

struct EvaluatorEnvironment;

// Generators output C/C++ code
typedef bool (*GeneratorFunc)(EvaluatorEnvironment& environment, const EvaluatorContext& context,
                              const std::vector<Token>& tokens, int startTokenIndex,
                              GeneratorOutput& output);

// Macros output tokens only
// Note that this is for the macro invocation/signature; defining macros is actually done via a
// generator, because macros themselves are implemented in Cakelisp/C++
typedef bool (*MacroFunc)(EvaluatorEnvironment& environment, const EvaluatorContext& context,
                          const std::vector<Token>& tokens, int startTokenIndex,
                          std::vector<Token>& output);

// TODO: Replace with fast hash table implementation
typedef std::unordered_map<std::string, MacroFunc> MacroTable;
typedef std::unordered_map<std::string, GeneratorFunc> GeneratorTable;
typedef MacroTable::iterator MacroIterator;
typedef GeneratorTable::iterator GeneratorIterator;

struct ObjectReference
{
	const std::vector<Token>* tokens;
	int startIndex;
	EvaluatorContext context;

	// This needs to be a pointer in case the references array gets moved while we are iterating it
	GeneratorOutput* spliceOutput;
	// Not used in ObjectReferenceStatus, only ReferencePools
	bool isResolved;
};

// TODO Need to add insertion points for later fixing
struct ObjectReferenceStatus
{
	const Token* name;
	// We need to guess and check because we don't know what C/C++ functions might be available. The
	// guessState keeps track of how successful the guess was, so we don't keep recompiling until
	// some relevant change to our references has occurred
	ObjectReferenceGuessState guessState;

	// In the case of multiple references to the same object in the same definition, keep track of
	// all of them for guessing
	std::vector<ObjectReference> references;
};

typedef std::unordered_map<std::string, ObjectReferenceStatus> ObjectReferenceStatusMap;
typedef std::pair<const std::string, ObjectReferenceStatus> ObjectReferenceStatusPair;
// typedef std::unordered_map<std::string, int> ObjectReferentMap;

struct ObjectDefinition
{
	const Token* name;
	ObjectType type;
	// Objects can be referenced by other objects, but something in the chain must be required in
	// order for the objects to be built. Required-ness spreads from the top level module scope
	bool isRequired;
	// Unique references, for dependency checking
	ObjectReferenceStatusMap references;

	// Used only for compile-time functions
	// Note that these don't need headers or metadata because they are found via dynamic linking.
	// GeneratorOutput is (somewhat wastefully) used in order to make the API consistent for
	// compile-time vs. runtime code generation
	GeneratorOutput* output;

	// For compile time functions, whether they have finished loading successfully. Mainly just a
	// shortcut to what isCompileTimeCodeLoaded() would return
	bool isLoaded;
};

struct ObjectReferencePool
{
	std::vector<ObjectReference> references;
};

// NOTE: See comment in BuildEvaluateReferences() before changing this data structure. The current
// implementation assumes references to values will not be invalidated if the hash map changes
typedef std::unordered_map<std::string, ObjectDefinition> ObjectDefinitionMap;
typedef std::pair<const std::string, ObjectDefinition> ObjectDefinitionPair;
typedef std::unordered_map<std::string, ObjectReferencePool> ObjectReferencePoolMap;
typedef std::pair<const std::string, ObjectReferencePool> ObjectReferencePoolPair;

// Unlike context, which can't be changed, environment can be changed.
// Use care when modifying the environment. Only add things once you know things have succeeded.
// Keep in mind that calling functions which can change the environment may invalidate your pointers
// if things resize.
struct EvaluatorEnvironment
{
	// Compile-time-executable functions
	MacroTable macros;
	GeneratorTable generators;

	// We need to keep the tokens macros create around so they can be referenced by StringOperations
	// Token vectors must not be changed after they are created or pointers to Tokens will become
	// invalid. The const here is to protect from that. You can change the token contents, however
	std::vector<const std::vector<Token>*> macroExpansions;

	ObjectDefinitionMap definitions;
	ObjectReferencePoolMap referencePools;

	// Used to ensure unique filenames for compile-time artifacts
	int nextFreeBuildId;
	// Ensure unique macro variable names, for example
	int nextFreeUniqueSymbolNum;

	// Used to load other files into the environment
	// If this is null, it means other Cakelisp files will not be imported (which could be desired)
	ModuleManager* moduleManager;

	// Generate code so that objects defined in Cakelisp can be reloaded at runtime
	bool enableHotReloading;

	// Added as a search directory for compile time code execution
	std::string cakelispSrcDir;

	ProcessCommand compileTimeBuildCommand;
	ProcessCommand compileTimeLinkCommand;
	ProcessCommand buildTimeBuildCommand;
	ProcessCommand buildTimeLinkCommand;

	// Will NOT clean up macroExpansions! Use environmentDestroyInvalidateTokens()
	~EvaluatorEnvironment();
};

// Make sure you're ready to do this! (see macroExpansions comment)
// Essentially, this means don't call this function unless you will NOT follow any Token pointers in
// GeneratorOutput, StringOutput, etc., because any of them could be pointers to macro-created
// tokens. Essentially, call this as late as possible
void environmentDestroyInvalidateTokens(EvaluatorEnvironment& environment);

int EvaluateGenerate_Recursive(EvaluatorEnvironment& environment, const EvaluatorContext& context,
                               const std::vector<Token>& tokens, int startTokenIndex,
                               GeneratorOutput& output);

// Delimiter template will be inserted between the outputs. Pass nullptr for no delimiter
int EvaluateGenerateAll_Recursive(EvaluatorEnvironment& environment,
                                  const EvaluatorContext& context, const std::vector<Token>& tokens,
                                  int startTokenIndex, const StringOutput* delimiterTemplate,
                                  GeneratorOutput& output);

// Returns whether all references were resolved successfully
bool EvaluateResolveReferences(EvaluatorEnvironment& environment);

const char* evaluatorScopeToString(EvaluatorScope expectedScope);

bool addObjectDefinition(EvaluatorEnvironment& environment, ObjectDefinition& definition);

extern const char* globalDefinitionName;
extern const char* cakelispWorkingDir;
