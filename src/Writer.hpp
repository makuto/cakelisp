#pragma once

#include "ConverterEnums.hpp"
#include "EvaluatorEnums.hpp"
#include "WriterEnums.hpp"

struct NameStyleSettings;
struct StringOutput;
struct GeneratorOutput;

struct WriterFormatSettings
{
	// Don't use newlines or spaces/tabs. Overrides other options
	bool uglyPrint = false;
	WriterFormatBraceStyle braceStyle = WriterFormatBraceStyle_Allman;
	WriterFormatIndentType indentStyle = WriterFormatIndentType_Spaces;
	int indentTabWidth = 2;
};

struct WriterOutputSettings
{
	const char* sourceCakelispFilename;
	const char* sourceHeading;
	const char* sourceFooter;
	const char* headerHeading;
	const char* headerFooter;
};

const char* importLanguageToString(ImportLanguage type);

bool writeGeneratorOutput(const GeneratorOutput& generatedOutput,
                          const NameStyleSettings& nameSettings,
                          const WriterFormatSettings& formatSettings,
                          const WriterOutputSettings& outputSettings);
