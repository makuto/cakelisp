#include "Writer.hpp"

#include "Converters.hpp"
#include "Evaluator.hpp"
#include "Tokenizer.hpp"
#include "Utilities.hpp"

// TODO: safe version of strcat
#include <stdarg.h>  // va_start
#include <stdio.h>
#include <string.h>

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

	printf("Wrote %s\n", destFilename);

	if (remove(srcFilename) != 0)
	{
		printf("Failed to remove %s\n", srcFilename);
		return false;
	}

	return true;
}

bool writeIfContentsNewer(const char* tempFilename, const char* outputFilename)
{
	bool verbose = false;
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
		if (verbose)
			printf("Destination file didn't exist. Writing\n");

		return moveFile(tempFilename, outputFilename);
	}
	else
	{
		if (verbose)
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
			if (verbose)
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

		if (verbose)
			printf("File changed. writing\n");

		fclose(newFile);
		fclose(oldFile);

		return moveFile(tempFilename, outputFilename);
	}
}

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
	// TODO: Good indentation
	return;
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

	if (outputOperation.modifiers & StringOutMod_SpaceBefore)
		Writer_Writef(state, " ");

	// TODO Validate flags for e.g. OpenParen | CloseParen, which shouldn't be allowed
	NameStyleMode mode = getNameStyleModeForFlags(nameSettings, outputOperation.modifiers);
	if (mode)
	{
		char convertedName[MAX_NAME_LENGTH] = {0};
		lispNameStyleToCNameStyle(mode, outputOperation.output.c_str(), convertedName,
		                          sizeof(convertedName), *outputOperation.startToken);
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

// This is annoyingly complex because splices allow things in source to write to header
void writeOutputFollowSplices_Recursive(const NameStyleSettings& nameSettings,
                                        const WriterFormatSettings& formatSettings,
                                        StringOutputState& outputState,
                                        StringOutputState& otherOutputState,
                                        const std::vector<StringOutput>& outputOperations,
                                        bool isHeader)
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
				if (isHeader)
				{
					writeOutputFollowSplices_Recursive(
					    nameSettings, formatSettings, otherOutputState, outputState,
					    operation.spliceOutput->source, /*isHeader=*/false);
					writeOutputFollowSplices_Recursive(
					    nameSettings, formatSettings, outputState, otherOutputState,
					    operation.spliceOutput->header, /*isHeader=*/true);
				}
				else
				{
					writeOutputFollowSplices_Recursive(
					    nameSettings, formatSettings, outputState, otherOutputState,
					    operation.spliceOutput->source, /*isHeader=*/false);
					writeOutputFollowSplices_Recursive(
					    nameSettings, formatSettings, otherOutputState, outputState,
					    operation.spliceOutput->header, /*isHeader=*/true);
				}
			}
			else
				ErrorAtToken(*operation.startToken, "expected splice output");
		}
		else
			writeStringOutput(nameSettings, formatSettings, operation, outputState);
	}
}

bool writeOutputs(const NameStyleSettings& nameSettings, const WriterFormatSettings& formatSettings,
                  const WriterOutputSettings& outputSettings, const GeneratorOutput& outputToWrite)
{
	bool verbose = false;

	char sourceOutputName[MAX_PATH_LENGTH] = {0};
	PrintfBuffer(sourceOutputName, "%s.cpp", outputSettings.sourceCakelispFilename);
	char headerOutputName[MAX_PATH_LENGTH] = {0};
	PrintfBuffer(headerOutputName, "%s.hpp", outputSettings.sourceCakelispFilename);

	struct
	{
		bool isHeader;
		const char* outputFilename;
		const char* heading;
		const char* footer;

		StringOutputState outputState;
		// To determine if anything was actually written
		StringOutputState stateBeforeOutputWrite;
		char tempFilename[MAX_PATH_LENGTH];
	} outputs[] = {{/*isHeader=*/false,
	                sourceOutputName,
	                outputSettings.sourceHeading,
	                outputSettings.sourceFooter,
	                {},
	                {},
	                {0}},
	               {
	                   /*isHeader=*/true,
	                   headerOutputName,
	                   outputSettings.headerHeading,
	                   outputSettings.headerFooter,
	                   {},
	                   {},
	                   {0},
	               }};

	for (int i = 0; i < static_cast<int>(ArraySize(outputs)); ++i)
	{
		// Write to a temporary file
		PrintfBuffer(outputs[i].tempFilename, "%s.temp", outputs[i].outputFilename);
		// TODO: If this fails to open, Writer_Writef just won't write to the file, it'll print
		outputs[i].outputState.fileOut = fileOpen(outputs[i].tempFilename, "w");

		if (outputs[i].heading)
		{
			Writer_Writef(outputs[i].outputState, outputs[i].heading);
			// TODO: Put this in Writer_Writef
			for (const char* c = outputs[i].heading; *c != '\0'; ++c)
			{
				if (*c == '\n')
					++outputs[i].outputState.currentLine;
			}
		}

		outputs[i].stateBeforeOutputWrite = outputs[i].outputState;
	}

	// Source
	writeOutputFollowSplices_Recursive(nameSettings, formatSettings, outputs[0].outputState,
	                                   outputs[1].outputState, outputToWrite.source,
	                                   outputs[0].isHeader);
	// Header
	writeOutputFollowSplices_Recursive(nameSettings, formatSettings, outputs[1].outputState,
	                                   outputs[0].outputState, outputToWrite.header,
	                                   outputs[1].isHeader);

	for (int i = 0; i < static_cast<int>(ArraySize(outputs)); ++i)
	{
		// No output to this file. Don't write anything
		if (outputs[i].outputState.numCharsOutput ==
		    outputs[i].stateBeforeOutputWrite.numCharsOutput)
		{
			if (verbose)
				printf("%s had no meaningful output\n", outputs[i].tempFilename);

			fclose(outputs[i].outputState.fileOut);
			outputs[i].outputState.fileOut = nullptr;

			if (remove(outputs[i].tempFilename) != 0)
			{
				printf("Error: Failed to remove %s\n", outputs[i].tempFilename);
				return false;
			}
			continue;
		}

		if (outputs[i].footer)
		{
			Writer_Writef(outputs[i].outputState, outputs[i].footer);
			// TODO: Put this in Writer_Writef
			for (const char* c = outputs[i].footer; *c != '\0'; ++c)
			{
				if (*c == '\n')
					++outputs[i].outputState.currentLine;
			}
		}

		if (outputs[i].outputState.fileOut)
		{
			fclose(outputs[i].outputState.fileOut);
			outputs[i].outputState.fileOut = nullptr;
		}

		if (!writeIfContentsNewer(outputs[i].tempFilename, outputs[i].outputFilename))
			return false;
	}

	return true;
}

bool writeGeneratorOutput(const GeneratorOutput& generatedOutput,
                          const NameStyleSettings& nameSettings,
                          const WriterFormatSettings& formatSettings,
                          const WriterOutputSettings& outputSettings)
{
	bool verbose = false;

	if (!writeOutputs(nameSettings, formatSettings, outputSettings, generatedOutput))
		return false;

	// TODO: Write mapping and metadata
	if (verbose)
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
