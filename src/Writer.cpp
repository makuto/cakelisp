#include "Writer.hpp"

#include "Converters.hpp"
#include "ParserGenerator.hpp"
#include "Tokenizer.hpp"
#include "Utilities.hpp"

// TODO: safe version of strcat
#include <stdio.h>
#include <string.h>

const char* importLanguageToString(ImportLanguage type)
{
	switch (type)
	{
		case ImportLanguage_None:
			return "None";
		case ImportLanguage_C:
			return "C";
		case ImportLanguage_Cakelisp:
			return "Cakelisp";
		default:
			return "Unknown";
	}
}

// TODO This should be automatically generated
static NameStyleMode getNameStyleModeForFlags(const NameStyleSettings& settings,
                                              StringOutputModifierFlags modifierFlags)
{
	NameStyleMode mode = NameStyleMode_None;
	int numMatchingFlags = 0;
	if (modifierFlags & StringOutMod_ConvertTypeName)
	{
		++numMatchingFlags;
		mode = settings.typeNameMode;

		static bool hasWarned = false;
		if (!hasWarned && mode == NameStyleMode_PascalCase)
		{
			hasWarned = true;
			printf(
			    "\nWarning: Use of PascalCase for type names is discouraged because it will "
			    "destroy lowercase C type names. You should use PascalCaseIfPlural instead, which "
			    "will only apply case changes if the name looks lisp-y. This warning will only "
			    "appear once.\n");
		}
	}
	if (modifierFlags & StringOutMod_ConvertFunctionName)
	{
		++numMatchingFlags;
		mode = settings.functionNameMode;
	}
	if (modifierFlags & StringOutMod_ConvertArgumentName)
	{
		++numMatchingFlags;
		mode = settings.argumentNameMode;
	}
	if (modifierFlags & StringOutMod_ConvertVariableName)
	{
		++numMatchingFlags;
		mode = settings.variableNameMode;
	}
	if (modifierFlags & StringOutMod_ConvertGlobalVariableName)
	{
		++numMatchingFlags;
		mode = settings.globalVariableNameMode;
	}

	if (numMatchingFlags > 1)
		printf("\nWarning: Name was given conflicting convert flags: %d\n", (int)modifierFlags);

	return mode;
}

struct StringOutputState
{
	int blockDepth;
	// For determining character offsets to pass to e.g. Emacs' goto-char
	int numCharsOutput;
	int currentLine;
	FILE* fileOut;
};

// TODO Have writer scan strings for \n?
static void Writer_Writef(StringOutputState& state, const char* format, ...)
{
	va_list args;
	va_start(args, format);
	if (state.fileOut)
	{
		state.numCharsOutput += vfprintf(state.fileOut, format, args);
	}
	else
	{
		state.numCharsOutput += vprintf(format, args);
	}
	va_end(args);
}

static void printIndentation(const WriterFormatSettings& formatSettings, StringOutputState& state)
{
	if (formatSettings.indentStyle == WriterFormatIndentType_Tabs)
	{
		for (int i = 0; i < state.blockDepth; ++i)
		{
			Writer_Writef(state, "\t");
		}
	}
	else if (formatSettings.indentStyle == WriterFormatIndentType_Spaces)
	{
		for (int i = 0; i < state.blockDepth; ++i)
		{
			for (int spaces = 0; spaces < formatSettings.indentTabWidth; ++spaces)
				Writer_Writef(state, " ");
		}
	}
}

static void printStringOutput(const NameStyleSettings& nameSettings,
                              const WriterFormatSettings& formatSettings,
                              const StringOutput& outputOperation, StringOutputState& state)
{
	// First, handle indentation
	printIndentation(formatSettings, state);
	if (outputOperation.modifiers & StringOutMod_OpenBlock)
	{
		state.blockDepth++;
	}
	else if (outputOperation.modifiers & StringOutMod_CloseBlock)
	{
		state.blockDepth--;
		if (state.blockDepth < 0)
		{
			ErrorAtToken(*outputOperation.startToken,
			             "Evaluation resulted in mismatching curly braces");
		}
	}

	// TODO Validate flags for e.g. OpenParen | CloseParen, which shouldn't be allowed
	NameStyleMode mode = getNameStyleModeForFlags(nameSettings, outputOperation.modifiers);
	if (mode)
	{
		char convertedName[MAX_NAME_LENGTH] = {0};
		lispNameStyleToCNameStyle(mode, outputOperation.output.c_str(), convertedName,
		                          sizeof(convertedName));
		Writer_Writef(state, "%s", convertedName);
	}
	else if (outputOperation.modifiers & StringOutMod_SurroundWithQuotes)
		Writer_Writef(state, "\"%s\"", outputOperation.output.c_str());
	// Just by changing these we can change the output formatting
	else if (outputOperation.modifiers & StringOutMod_OpenBlock)
	{
		if (formatSettings.uglyPrint)
			Writer_Writef(state, "{");
		else if (formatSettings.braceStyle == WriterFormatBraceStyle_Allman)
		{
			Writer_Writef(state, "\n{\n");
			state.currentLine += 2;
		}
		else if (formatSettings.braceStyle == WriterFormatBraceStyle_KandR_1TBS)
		{
			Writer_Writef(state, "{\n");
			state.currentLine += 1;
		}
	}
	else if (outputOperation.modifiers & StringOutMod_CloseBlock)
	{
		if (formatSettings.uglyPrint)
			Writer_Writef(state, "}");
		else
		{
			Writer_Writef(state, "}\n");
			++state.currentLine;
		}
	}
	else if (outputOperation.modifiers & StringOutMod_OpenParen)
		Writer_Writef(state, "(");
	else if (outputOperation.modifiers & StringOutMod_CloseParen)
		Writer_Writef(state, ")");
	else if (outputOperation.modifiers & StringOutMod_EndStatement)
	{
		if (formatSettings.uglyPrint)
			Writer_Writef(state, ";");
		else
		{
			Writer_Writef(state, ";\n");
			++state.currentLine;
		}

		printIndentation(formatSettings, state);
	}
	else if (outputOperation.modifiers & StringOutMod_ListSeparator)
		Writer_Writef(state, ", ");
	else
		Writer_Writef(state, "%s", outputOperation.output.c_str());

	// We assume we cannot ignore these even in ugly print mode
	if (outputOperation.modifiers & StringOutMod_SpaceAfter)
		Writer_Writef(state, " ");
	if (outputOperation.modifiers & StringOutMod_NewlineAfter)
	{
		Writer_Writef(state, "\n");
		++state.currentLine;
	}
}

FILE* fileOpen(const char* filename)
{
	FILE* file = nullptr;
	file = fopen(filename, "w");
	if (!file)
	{
		printf("Error: Could not open %s\n", filename);
		return nullptr;
	}
	else
	{
		printf("Opened %s\n", filename);
	}
	return file;
}

void printGeneratorOutput(const GeneratorOutput& generatedOutput,
                          const NameStyleSettings& nameSettings,
                          const WriterFormatSettings& formatSettings,
                          const WriterOutputSettings& outputSettings)
{
	printf("\tTo source file:\n");
	{
		StringOutputState sourceState = {};
		if (outputSettings.sourceCakelispFilename)
		{
			char generatedSourceFilename[MAX_PATH_LENGTH] = {0};
			PrintfBuffer(generatedSourceFilename, "%s.cpp", outputSettings.sourceCakelispFilename);
			sourceState.fileOut = fileOpen(generatedSourceFilename);
		}

		for (const StringOutput& operation : generatedOutput.source)
		{
			// Debug print mapping
			if (!operation.output.empty())
			{
				printf("%s \t%d\tline %d\n", operation.output.c_str(),
				       sourceState.numCharsOutput + 1, sourceState.currentLine + 1);
			}

			printStringOutput(nameSettings, formatSettings, operation, sourceState);
		}
		printf("Wrote %d characters\n", sourceState.numCharsOutput);

		if (sourceState.fileOut)
		{
			fclose(sourceState.fileOut);
			sourceState.fileOut = nullptr;
		}
	}

	printf("\n\tTo header file:\n");
	{
		StringOutputState headerState = {};
		if (outputSettings.sourceCakelispFilename)
		{
			char generatedSourceFilename[MAX_PATH_LENGTH] = {0};
			PrintfBuffer(generatedSourceFilename, "%s.hpp", outputSettings.sourceCakelispFilename);
			headerState.fileOut = fileOpen(generatedSourceFilename);
		}

		for (const StringOutput& operation : generatedOutput.header)
		{
			printStringOutput(nameSettings, formatSettings, operation, headerState);
		}

		if (headerState.fileOut)
		{
			fclose(headerState.fileOut);
			headerState.fileOut = nullptr;
		}
	}

	// Metadata
	printf("\n\tImports:\n");
	for (const ImportMetadata& import : generatedOutput.imports)
	{
		printf("%s\t(%s)\n", import.importName.c_str(), importLanguageToString(import.language));
	}

	printf("\n\tFunctions:\n");
	for (const FunctionMetadata& function : generatedOutput.functions)
	{
		printf("%s\n", function.name.c_str());
		if (!function.arguments.empty())
		{
			printf("(\n");
			for (const FunctionArgumentMetadata& argument : function.arguments)
			{
				printf("\t%s\n", argument.name.c_str());
			}
			printf(")\n");
		}
		else
			printf("()\n");

		printf("\n");
	}
}
