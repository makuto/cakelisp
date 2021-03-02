#pragma once

#include <vector>

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
	WriterFormatIndentType indentStyle = WriterFormatIndentType_Tabs;
	int indentTabWidth = 4;
};

struct WriterOutputSettings
{
	const char* sourceCakelispFilename;

	const char* sourceOutputName;
	const char* headerOutputName;

	// User code has less control over these outputs. These are more internal/automatic, e.g.
	// handling automatic includes or extern "C" wrapping
	// Note that these cover both the source and header heading and footer
	const GeneratorOutput* heading;
	const GeneratorOutput* footer;
};

const char* importLanguageToString(ImportLanguage type);

bool writeGeneratorOutput(const GeneratorOutput& generatedOutput,
                          const NameStyleSettings& nameSettings,
                          const WriterFormatSettings& formatSettings,
                          const WriterOutputSettings& outputSettings);

// Create combinedHeaderFilename which is a header that includes headersToInclude
bool writeCombinedHeader(const char* combinedHeaderFilename,
                         std::vector<const char*>& headersToInclude);
