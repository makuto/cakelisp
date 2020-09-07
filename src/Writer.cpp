#include "Writer.hpp"

#include "Converters.hpp"
#include "Evaluator.hpp"
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
	if (modifierFlags & StringOutMod_ConvertVariableName)
	{
		++numMatchingFlags;
		mode = settings.variableNameMode;
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

static void writeStringOutput(const NameStyleSettings& nameSettings,
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
	else if (outputOperation.modifiers & StringOutMod_OpenList)
		Writer_Writef(state, "{");
	else if (outputOperation.modifiers & StringOutMod_CloseList)
		Writer_Writef(state, "}");
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

bool moveFile(const char* srcFilename, const char* destFilename)
{
	FILE* srcFile = fopen(srcFilename, "r");
	FILE* destFile = fopen(destFilename, "w");
	if (!srcFile || !destFile)
		return false;

	char buffer[1024];
	while (fgets(buffer, sizeof(buffer), srcFile))
		fputs(buffer, destFile);

	fclose(srcFile);
	fclose(destFile);

	if (remove(srcFilename) != 0)
	{
		printf("Failed to remove %s\n", srcFilename);
		return false;
	}

	return true;
}

void writeOutputFollowSplices_Recursive(const NameStyleSettings& nameSettings,
                                        const WriterFormatSettings& formatSettings,
                                        StringOutputState& outputState,
                                        const std::vector<StringOutput>& outputOperations)
{
	for (const StringOutput& operation : outputOperations)
	{
		// Debug print mapping
		if (!operation.output.empty() && false)
		{
			printf("%s \t%d\tline %d\n", operation.output.c_str(), outputState.numCharsOutput + 1,
			       outputState.currentLine + 1);
		}

		if (operation.modifiers == StringOutMod_Splice)
		{
			if (operation.spliceOutput)
			{
				writeOutputFollowSplices_Recursive(nameSettings, formatSettings, outputState,
				                                   operation.spliceOutput->source);
				if (!operation.spliceOutput->header.empty())
					ErrorAtToken(*operation.startToken,
					             "splice output contained header output, which is not supported");
			}
			else
				ErrorAtToken(*operation.startToken, "expected splice output");
		}
		else
			writeStringOutput(nameSettings, formatSettings, operation, outputState);
	}
}

// TODO This sucks
// TODO Lots of params
bool writeIfContentsNewer(const NameStyleSettings& nameSettings,
                          const WriterFormatSettings& formatSettings,
                          const WriterOutputSettings& outputSettings, const char* heading,
                          const char* footer, const std::vector<StringOutput>& outputOperations,
                          const char* outputFilename)
{
	// Write to a temporary file
	StringOutputState outputState = {};
	char tempFilename[MAX_PATH_LENGTH] = {0};
	PrintfBuffer(tempFilename, "%s.temp", outputFilename);
	// TODO: If this fails to open, Writer_Writef just won't write to the file, it'll print
	outputState.fileOut = fileOpen(tempFilename);

	if (heading)
	{
		Writer_Writef(outputState, heading);
		// TODO: Put this in Writer_Writef
		for (const char* c = heading; *c != '\0'; ++c)
		{
			if (*c == '\n')
				++outputState.currentLine;
		}
	}

	writeOutputFollowSplices_Recursive(nameSettings, formatSettings, outputState, outputOperations);

	if (footer)
	{
		Writer_Writef(outputState, footer);
		// TODO: Put this in Writer_Writef
		for (const char* c = footer; *c != '\0'; ++c)
		{
			if (*c == '\n')
				++outputState.currentLine;
		}
	}

	if (outputState.fileOut)
	{
		fclose(outputState.fileOut);
		outputState.fileOut = nullptr;
	}

	// Read temporary file and destination file and compare
	FILE* newFile = fopen(tempFilename, "r");
	if (!newFile)
	{
		printf("Error: Could not open %s\n", tempFilename);
		return false;
	}
	FILE* oldFile = fopen(outputFilename, "r");
	if (!oldFile)
	{
		// Write new and remove temp
		printf("Destination file didn't exist. Writing\n");
		return moveFile(tempFilename, outputFilename);
	}
	else
	{
		printf("Destination file exists. Comparing\n");
		char newBuffer[1024] = {0};
		char oldBuffer[1024] = {0};
		bool identical = true;
		while (1)
		{
			bool newBufferRead = fgets(newBuffer, sizeof(newBuffer), newFile) != nullptr;
			bool oldBufferRead = fgets(oldBuffer, sizeof(oldBuffer), oldFile) != nullptr;
			if (!newBufferRead || !oldBufferRead)
			{
				if (newBufferRead != oldBufferRead)
					identical = false;
				break;
			}

			if (memcmp(newBuffer, oldBuffer, sizeof(newBuffer)) != 0)
			{
				identical = false;
				break;
			}
		}

		if (identical)
		{
			printf("Files are identical. Skipping\n");
			fclose(newFile);
			fclose(oldFile);
			if (remove(tempFilename) != 0)
			{
				printf("Failed to remove %s\n", tempFilename);
				return false;
			}
			return true;
		}

		printf("File changed. writing\n");
		fclose(newFile);
		fclose(oldFile);

		return moveFile(tempFilename, outputFilename);
	}
}

bool writeGeneratorOutput(const GeneratorOutput& generatedOutput,
                          const NameStyleSettings& nameSettings,
                          const WriterFormatSettings& formatSettings,
                          const WriterOutputSettings& outputSettings)
{
	if (!generatedOutput.source.empty())
	{
		printf("\tTo source file:\n");
		{
			char sourceOutputName[MAX_PATH_LENGTH] = {0};
			PrintfBuffer(sourceOutputName, "%s.cpp", outputSettings.sourceCakelispFilename);
			if (!writeIfContentsNewer(nameSettings, formatSettings, outputSettings,
			                          outputSettings.sourceHeading, outputSettings.sourceFooter,
			                          generatedOutput.source, sourceOutputName))
				return false;
		}
	}

	if (!generatedOutput.header.empty())
	{
		printf("\n\tTo header file:\n");
		{
			char headerOutputName[MAX_PATH_LENGTH] = {0};
			PrintfBuffer(headerOutputName, "%s.hpp", outputSettings.sourceCakelispFilename);
			if (!writeIfContentsNewer(nameSettings, formatSettings, outputSettings,
			                          outputSettings.headerHeading, outputSettings.headerFooter,
			                          generatedOutput.header, headerOutputName))
				return false;
		}
	}

	// TODO: Write mapping and metadata
	if (false)
	{
		// Metadata
		printf("\n\tImports:\n");
		for (const ImportMetadata& import : generatedOutput.imports)
		{
			printf("%s\t(%s)\n", import.importName.c_str(),
			       importLanguageToString(import.language));
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

	return true;
}
