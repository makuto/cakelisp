#include <stdio.h>
#include <string.h>

#include <vector>

#include "Converters.hpp"
#include "Evaluator.hpp"
#include "Generators.hpp"
#include "ModuleManager.hpp"
#include "OutputPreambles.hpp"
#include "RunProcess.hpp"
#include "Tokenizer.hpp"
#include "Utilities.hpp"
#include "Writer.hpp"

int main(int argc, char* argv[])
{
	if (argc != 2)
	{
		printf("Need to provide a file to parse\n");
		return 1;
	}

	printf("\nTokenization:\n");

	const char* filename = argv[1];

	const std::vector<Token>* tokens = nullptr;
	if (!moduleLoadTokenizeValidate(filename, &tokens))
		return 1;

	printf("\nParsing and code generation:\n");

	ModuleManager moduleManager = {};
	moduleManagerInitialize(moduleManager);
	EvaluatorContext moduleContext = {};
	moduleContext.scope = EvaluatorScope_Module;
	moduleContext.definitionName = &moduleManager.globalPseudoInvocationName;
	// Module always requires all its functions
	// TODO: Local functions can be left out if not referenced (in fact, they may warn in C if not)
	moduleContext.isRequired = true;
	GeneratorOutput generatedOutput;
	StringOutput bodyDelimiterTemplate = {};
	bodyDelimiterTemplate.modifiers = StringOutMod_NewlineAfter;
	int numErrors = EvaluateGenerateAll_Recursive(moduleManager.environment, moduleContext, *tokens,
	                                              /*startTokenIndex=*/0, bodyDelimiterTemplate,
	                                              generatedOutput);
	if (numErrors)
	{
		environmentDestroyInvalidateTokens(moduleManager.environment);
		delete tokens;
		return 1;
	}

	if (!EvaluateResolveReferences(moduleManager.environment))
	{
		environmentDestroyInvalidateTokens(moduleManager.environment);
		delete tokens;
		return 1;
	}

	// Final output
	{
		NameStyleSettings nameSettings;
		WriterFormatSettings formatSettings;
		WriterOutputSettings outputSettings;
		outputSettings.sourceCakelispFilename = filename;

		char sourceHeadingBuffer[1024] = {0};
		// TODO: hpp to h support
		// TODO: Strip path from filename
		PrintfBuffer(sourceHeadingBuffer, "#include \"%s.hpp\"\n%s", filename,
		             generatedSourceHeading ? generatedSourceHeading : "");
		outputSettings.sourceHeading = sourceHeadingBuffer;
		outputSettings.sourceFooter = generatedSourceFooter;
		outputSettings.headerHeading = generatedHeaderHeading;
		outputSettings.headerFooter = generatedHeaderFooter;

		printf("\nResult:\n");

		if (!writeGeneratorOutput(generatedOutput, nameSettings, formatSettings, outputSettings))
		{
			environmentDestroyInvalidateTokens(moduleManager.environment);
			delete tokens;
			return 1;
		}
	}

	environmentDestroyInvalidateTokens(moduleManager.environment);
	delete tokens;
	return 0;
}
