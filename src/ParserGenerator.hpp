#pragma once

#include "Tokenizer.hpp"
#include "Converters.hpp"

#include <string>
#include <vector>

enum StringOutputModifierFlags
{
	StringOutMod_None = 0,
	StringOutMod_NewlineAfter = 1 << 0,
	StringOutMod_SpaceAfter = 1 << 1,

	// Don't add to the beginning/end without updating the start and end values
	StringOutMod_ConvertTypeName = 1 << 2,
	StringOutMod_ConvertFunctionName = 1 << 3,
	StringOutMod_ConvertArgumentName = 1 << 4,
	StringOutMod_ConvertVariableName = 1 << 5,
	StringOutMod_ConvertGlobalVariableName = 1 << 6,

	StringOutMod_NameConverters_START = StringOutMod_ConvertTypeName,
	StringOutMod_NameConverters_END = StringOutMod_ConvertGlobalVariableName,
};

struct StringOutput
{
	std::string output;
	StringOutputModifierFlags modifiers;

	// Used to correlate Cakelisp code with generated output code
	const Token* startToken;
	const Token* endToken;
};

struct FunctionMetadata
{
	// The Cakelisp name, NOT the converted C name
	std::string name;
	const Token* startToken;
	const Token* endToken;
};

enum ImportType
{
	ImportType_None = 0,
	ImportType_C,
	ImportType_Cakelisp
};

const char* importTypeToString(ImportType type);

struct ImportMetadata
{
	std::string importName;
	ImportType type;
	const Token* triggerToken;
};

struct GeneratorOutput
{
	std::vector<StringOutput> source;
	std::vector<StringOutput> header;

	std::vector<FunctionMetadata> functions;
	std::vector<ImportMetadata> imports;
};

int parserGenerateCode(const std::vector<Token>& tokens, GeneratorOutput& output);

void debugPrintStringOutput(NameStyleSettings& settings, const StringOutput& outputOperation);
