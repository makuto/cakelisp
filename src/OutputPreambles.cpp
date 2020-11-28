#include "OutputPreambles.hpp"

#include "Evaluator.hpp"
#include "GeneratorHelpers.hpp"
#include "Utilities.hpp"

void makeCompileTimeHeaderFooter(GeneratorOutput& headerOut, GeneratorOutput& footerOut,
                                 GeneratorOutput* spliceAfterHeaders, const Token* blameToken)
{
	const char* defaultIncludes[] = {
	    "Evaluator.hpp", "EvaluatorEnums.hpp", "Tokenizer.hpp", "GeneratorHelpers.hpp",
	    "Utilities.hpp", "ModuleManager.hpp",  "Converters.hpp"};

	for (unsigned int i = 0; i < ArraySize(defaultIncludes); ++i)
	{
		addStringOutput(headerOut.source, "#include", StringOutMod_SpaceAfter, blameToken);
		addStringOutput(headerOut.source, defaultIncludes[i], StringOutMod_SurroundWithQuotes,
		                blameToken);
		addLangTokenOutput(headerOut.source, StringOutMod_NewlineAfter, blameToken);
	}

	addLangTokenOutput(headerOut.source, StringOutMod_NewlineAfter, blameToken);

	// Allow the caller space to add things after the includes but before the extern "C"
	if (spliceAfterHeaders)
	{
		addSpliceOutput(headerOut, spliceAfterHeaders, blameToken);

		addLangTokenOutput(headerOut.source, StringOutMod_NewlineAfter, blameToken);
	}

	// Must use extern "C" for dynamic symbols, because otherwise name mangling makes things hard
	addStringOutput(headerOut.source, "extern \"C\" {", StringOutMod_NewlineAfter, blameToken);

	addStringOutput(footerOut.source, "}", StringOutMod_None, blameToken);
}

void makeRunTimeHeaderFooter(GeneratorOutput& headerOut, GeneratorOutput& footerOut,
                             const Token* blameToken)
{
	addStringOutput(headerOut.header, "#pragma once", StringOutMod_NewlineAfter, blameToken);
}
