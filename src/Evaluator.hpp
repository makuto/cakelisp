#pragma once

#include <string>
#include <vector>
// TODO: Replace with fast hash table
#include <unordered_map>

#include "Build.hpp"
#include "EvaluatorEnums.hpp"
#include "Exporting.hpp"
#include "FileTypes.hpp"
#include "RunProcess.hpp"

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
	// union {
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

	// Whether or not the code in this context is required to be generated, i.e. isn't "dead" code
	bool isRequired;
	// Associate all unknown references with this definition
	const Token* definitionName;
	// When resolving references, have the context keep track of which reference is being resolved,
	// to avoid creating additional references unnecessarily
	const Token* resolvingReference;
	Module* module;
	// Insert delimiterTemplate between each expression/statement. Only recognized in
	// EvaluateGenerateAll_Recursive()
	StringOutput delimiterTemplate;
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

typedef std::unordered_map<std::string, const Token*> GeneratorLastReferenceTable;
typedef GeneratorLastReferenceTable::iterator GeneratorLastReferenceTableIterator;

struct ObjectReference
{
	const std::vector<Token>* tokens;
	int startIndex;
	EvaluatorContext context;

	ObjectReferenceResolutionType type;

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

struct MacroExpansion
{
	const Token* atToken;
	const std::vector<Token>* tokens;
};

struct ObjectDefinition
{
	std::string name;
	// The generator invocation that actually triggered the definition of this object
	const Token* definitionInvocation;
	ObjectType type;

	// Objects can be referenced by other objects, but something in the chain must be required in
	// order for the objects to be built. Required-ness spreads from the top level module scope
	bool isRequired;
	// The user's code might not require it, but the environment does, so don't error if this
	// definition has no references
	bool environmentRequired;
	// If we learn this will always fail compilation, prevent it from continuously being recompiled
	bool forbidBuild;

	// Unique references, for dependency checking
	ObjectReferenceStatusMap references;

	// Used to prepare the definition's expanded form, for post-macro-expansion code modification.
	// EvaluatorEnvironment still handles deleting the tokens array the macro created. Note that
	// these won't necessarily be in order, because macro definitions could resolve in different
	// orders than invoked
	std::vector<MacroExpansion> macroExpansions;

	// If a definition is going to be modified, store its context for reevaluation
	EvaluatorContext context;

	// Both runtime and compile-time definitions use output. Runtime definitions use it to support
	// compile-time code modification (post-macro-expansion), and compile-time definitions use it to
	// stay out of runtime output (and be easily output to cache files for compilation)
	GeneratorOutput* output;

	// For compile time functions, whether they have finished loading successfully. Mainly just a
	// shortcut to what isCompileTimeCodeLoaded() would return
	bool isLoaded;
	// Used by other compile-time functions to include this function's already output header
	std::string compileTimeHeaderName;
	// Only necessary for Windows builds; holds the import library for calling comptime functions
	std::string compileTimeImportLibraryName;

	// Arbitrary tags user may add for compile-time reference
	std::vector<std::string> tags;

	// In order to have context-unique symbols, this number is incremented for each unique name
	// requested. This is only relevant for compile-time function bodies
	int nextFreeUniqueSymbolNum;
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

typedef std::unordered_map<std::string, void*> CompileTimeFunctionTable;
typedef CompileTimeFunctionTable::iterator CompileTimeFunctionTableIterator;

struct CompileTimeFunctionMetadata
{
	const Token* nameToken;
	const Token* startArgsToken;
};

typedef std::unordered_map<std::string, CompileTimeFunctionMetadata>
    CompileTimeFunctionMetadataTable;
typedef CompileTimeFunctionMetadataTable::iterator CompileTimeFunctionMetadataTableIterator;

// Always update both of these. Signature helps validate call
extern const char* g_environmentPreLinkHookSignature;
typedef bool (*PreLinkHook)(ModuleManager& manager, ProcessCommand& linkCommand,
                            ProcessCommandInput* linkTimeInputs, int numLinkTimeInputs);
extern const char* g_environmentPostReferencesResolvedHookSignature;
typedef bool (*PostReferencesResolvedHook)(EvaluatorEnvironment& environment,
                                           bool& wasCodeModifiedOut);

// Update g_environmentCompileTimeVariableDestroySignature if you change this signature
typedef void (*CompileTimeVariableDestroyFunc)(void* data);

struct CompileTimeVariable
{
	// For runtime type checking
	std::string type;
	void* data;
	// The type must be known if destructors are to be run. POD/C malloc'd types can use free()
	// (which is used if destroyCompileTimeFuncName is empty), but C++ types need to cast the
	// pointer to the appropriate type to make sure destructor is called
	std::string destroyCompileTimeFuncName;
};
typedef std::unordered_map<std::string, CompileTimeVariable> CompileTimeVariableTable;
typedef CompileTimeVariableTable::iterator CompileTimeVariableTableIterator;
typedef std::pair<const std::string, CompileTimeVariable> CompileTimeVariableTablePair;

typedef std::unordered_map<std::string, const char*> RequiredCompileTimeFunctionReasonsTable;
typedef RequiredCompileTimeFunctionReasonsTable::iterator
    RequiredCompileTimeFunctionReasonsTableIterator;

typedef std::unordered_map<std::string, bool> CompileTimeSymbolTable;

typedef std::unordered_map<std::string, FileModifyTime> HeaderModificationTimeTable;

// Unlike context, which can't be changed, environment can be changed.
// Keep in mind that calling functions which can change the environment may invalidate your pointers
// if things resize.
struct EvaluatorEnvironment
{
	// Compile-time-executable functions
	MacroTable macros;

	GeneratorTable generators;
	// If the user renames a generator, store it under its old name here. This prevents repeated
	// undefining of a generator, which might undefine generators which aren't built-in
	GeneratorTable renamedGenerators;
	// More badness to protect from RenameBuiltinGenerator problems. This table allows the user to
	// know they fully overrode the built-in before it was ever referenced (or not)
	GeneratorLastReferenceTable lastGeneratorReferences;

	// Dumping ground for functions without fixed signatures
	CompileTimeFunctionTable compileTimeFunctions;
	CompileTimeFunctionMetadataTable compileTimeFunctionInfo;
	RequiredCompileTimeFunctionReasonsTable requiredCompileTimeFunctions;

	// We need to keep the tokens macros etc. create around so they can be referenced by
	// StringOperations. Token vectors must not be changed after they are created or pointers to
	// Tokens will become invalid. The const here is to protect from that. You can change the token
	// contents, however
	std::vector<const std::vector<Token>*> comptimeTokens;

	// Shared across comptime build rounds
	HeaderModificationTimeTable comptimeHeaderModifiedCache;
	// If an existing cached build was run, check the current build's commands against the previous
	// commands via CRC comparison. This ensures changing commands will cause rebuilds
	ArtifactCrcTable comptimeCachedCommandCrcs;
	// If any artifact no longer matches its crc in cachedCommandCrcs, the change will appear here
	ArtifactCrcTable comptimeNewCommandCrcs;

	// When a definition is replaced (e.g. by ReplaceAndEvaluateDefinition()), the original
	// definition's output is still used, but no longer has a definition to keep track of it. We'll
	// make sure the orphans get destroyed
	std::vector<GeneratorOutput*> orphanedOutputs;

	ObjectDefinitionMap definitions;
	ObjectReferencePoolMap referencePools;

	// Used to ensure unique filenames for compile-time artifacts
	int nextFreeBuildId;
	// Ensure unique macro variable names, for example
	int nextFreeUniqueSymbolNum;

	// Used to load other files into the environment
	// If this is null, it means other Cakelisp files will not be imported (which could be desired)
	ModuleManager* moduleManager;

	// User-specified variables usable by any compile-time macro/generator/function/hook
	CompileTimeVariableTable compileTimeVariables;

	// User-defined symbols usable before/after compile-time compilation is possible
	CompileTimeSymbolTable compileTimeSymbols;

	// Generate code so that objects defined in Cakelisp can be loaded at runtime
	bool useCLinkage;

	// Whether to enable MSVC-specific hacks/conversions
	bool isMsvcCompiler;

	// Whether it is okay to skip an operation if the resultant file is already in the cache (and
	// the source file hasn't been modified more recently)
	bool useCachedFiles;

	// Added as a search directory for compile time code execution
	std::string cakelispSrcDir;

	// Search paths for Cakelisp files, not C includes
	std::vector<std::string> searchPaths;

	std::vector<std::string> cSearchDirectories;

	// Build configurations are e.g. Debug vs. Release, which have e.g. different compiler flags
	// Anything which changes the output vs. another configuration should have a label. Examples:
	// - Hot-reloading vs. static
	// - Debug vs. Release vs. Profile
	// - Windows vs. Unix
	// - x86 vs. Arm64
	// Labels make it possible for the user to minimize re-compilation by ensuring the cache retains
	// artifacts from the last configurations, so you can build debug and switch to release, then
	// back to debug without having to rebuild debug. Many labels can be specified
	std::vector<std::string> buildConfigurationLabels;

	// Once set, no label changes are allowed (the output is being written)
	bool buildConfigurationLabelsAreFinal;

	// When using the default build system, the path to output the final executable
	std::string executableOutput;

	ProcessCommand compileTimeBuildCommand;
	ProcessCommand compileTimeLinkCommand;
	ProcessCommand buildTimeBuildCommand;
	ProcessCommand buildTimeLinkCommand;

	// At this point, all known references are resolved. This is the best time to let the user do
	// arbitrary code generation and modification. These changes will need to be evaluated and their
	// references resolved, so we need to repeat the whole process until no more changes are made
	std::vector<PostReferencesResolvedHook> postReferencesResolvedHooks;

	// Gives the user the chance to change the link command
	std::vector<PreLinkHook> preLinkHooks;

	// Will NOT clean up macroExpansions! Use environmentDestroyInvalidateTokens()
	~EvaluatorEnvironment();
};

// Make sure you're ready to do this! (see macroExpansions comment)
// Essentially, this means don't call this function unless you will NOT follow any Token pointers in
// GeneratorOutput, StringOutput, etc., because any of them could be pointers to macro-created
// tokens. Essentially, call this as late as possible
void environmentDestroyInvalidateTokens(EvaluatorEnvironment& environment);

CAKELISP_API int EvaluateGenerate_Recursive(EvaluatorEnvironment& environment,
                                            const EvaluatorContext& context,
                                            const std::vector<Token>& tokens, int startTokenIndex,
                                            GeneratorOutput& output);

// Delimiter template will be inserted between the outputs. Pass nullptr for no delimiter
int EvaluateGenerateAll_Recursive(EvaluatorEnvironment& environment,
                                  const EvaluatorContext& context, const std::vector<Token>& tokens,
                                  int startTokenIndex, GeneratorOutput& output);

// For compile-time code modification.
// This destroys the old definition. Don't hold on to references to it for that reason
CAKELISP_API bool ReplaceAndEvaluateDefinition(EvaluatorEnvironment& environment,
                                               const char* definitionToReplaceName,
                                               const std::vector<Token>& newDefinitionTokens);

// Returns whether all references were resolved successfully
bool EvaluateResolveReferences(EvaluatorEnvironment& environment);

const char* evaluatorScopeToString(EvaluatorScope expectedScope);

bool addObjectDefinition(EvaluatorEnvironment& environment, ObjectDefinition& definition);
const ObjectReferenceStatus* addObjectReference(EvaluatorEnvironment& environment,
                                                const Token& referenceNameToken,
                                                ObjectReference& reference);

// When registering new generators, this function will re-evaluate any existing references that
// would otherwise be guessed incorrectly, now that we know it's a generator
bool registerEvaluateGenerator(EvaluatorEnvironment& environment, const char* generatorName,
                               GeneratorFunc function);

GeneratorFunc findGenerator(EvaluatorEnvironment& environment, const char* functionName);
CAKELISP_API void* findCompileTimeFunction(EvaluatorEnvironment& environment,
                                           const char* functionName);
bool findCompileTimeSymbol(EvaluatorEnvironment& environment, const char* symbolName);
CAKELISP_API ObjectDefinition* findObjectDefinition(EvaluatorEnvironment& environment,
                                                    const char* name);

// These must take type as string in order to be address agnostic, making caching possible
// destroyFunc is necessary for any C++ type with a destructor. If nullptr, free() is used
CAKELISP_API bool CreateCompileTimeVariable(EvaluatorEnvironment& environment, const char* name,
                                            const char* typeExpression, void* data,
                                            const char* destroyCompileTimeFuncName);
CAKELISP_API bool GetCompileTimeVariable(EvaluatorEnvironment& environment, const char* name,
                                         const char* typeExpression, void** dataOut);

// Whether the (reference) cached file is more recent than the filename, meaning whatever operation
// which made the cached file can be skipped. Basically fileIsMoreRecentlyModified(), but respects
// --ignore-cache
bool canUseCachedFile(EvaluatorEnvironment& environment, const char* filename,
                      const char* reference);

const char* objectTypeToString(ObjectType type);

// shortPath can be "Example.cake" or e.g. "../tests/Example.cake"
// encounteredInFile becomes an automatic relative search path
// Returns false if the file does not exist in any of the paths searched
bool searchForFileInPaths(const char* shortPath, const char* encounteredInFile,
                          const std::vector<std::string>& searchPaths, char* foundFilePathOut,
                          int foundFilePathOutSize);
bool searchForFileInPathsWithError(const char* shortPath, const char* encounteredInFile,
                                   const std::vector<std::string>& searchPaths,
                                   char* foundFilePathOut, int foundFilePathOutSize,
                                   const Token& blameToken);

extern const char* globalDefinitionName;
extern const char* cakelispWorkingDir;
