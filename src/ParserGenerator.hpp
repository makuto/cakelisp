#pragma once

#include "Converters.hpp"
#include "Tokenizer.hpp"

#include <string>
#include <vector>

enum StringOutputModifierFlags
{
	StringOutMod_None = 0,
	StringOutMod_NewlineAfter = 1 << 0,
	StringOutMod_SpaceAfter = 1 << 1,

	StringOutMod_ConvertTypeName = 1 << 2,
	StringOutMod_ConvertFunctionName = 1 << 3,
	StringOutMod_ConvertArgumentName = 1 << 4,
	StringOutMod_ConvertVariableName = 1 << 5,
	StringOutMod_ConvertGlobalVariableName = 1 << 6,

	StringOutMod_SurroundWithQuotes = 1 << 7,

	// StringOutMod_NameConverters_START = StringOutMod_ConvertTypeName,
	// StringOutMod_NameConverters_END = StringOutMod_ConvertGlobalVariableName,
};

// Rather than needing to allocate and edit a buffer eventually equal to the size of the final
// output, store output operations instead. This also facilitates source <-> generated mapping data
struct StringOutput
{
	std::string output;
	StringOutputModifierFlags modifiers;

	// Used to correlate Cakelisp code with generated output code
	const Token* startToken;
	const Token* endToken;
};

// Types can contain macro invocations. This should be used whenever programmatically dealing with
// types, otherwise you'll need to handle the macro expansion yourself
struct ExpandedType
{
	std::vector<Token> type;
	const Token* preExpansionStart;
	const Token* preExpansionEnd;
};

struct FunctionArgumentMetadata
{
	ExpandedType type;
	const Token* name;
};

struct FunctionMetadata
{
	// The Cakelisp name, NOT the converted C name
	std::string name;
	const Token* startToken;
	const Token* endToken;

	std::vector<FunctionArgumentMetadata> arguments;
};

enum ImportLanguage
{
	ImportLanguage_None = 0,
	ImportLanguage_C,
	ImportLanguage_Cakelisp
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

int EvaluateGenerate_Recursive(const std::vector<Token>& tokens, int startTokenIndex,
                               GeneratorOutput& output);

void debugPrintStringOutput(NameStyleSettings& settings, const StringOutput& outputOperation);
void printGeneratorOutput(const GeneratorOutput& generatedOutput,
                          const NameStyleSettings& nameSettings);
