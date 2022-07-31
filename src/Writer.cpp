#include "Writer.hpp"

#include "Converters.hpp"
#include "Evaluator.hpp"
#include "FileUtilities.hpp"
#include "Logging.hpp"
#include "Tokenizer.hpp"
#include "Utilities.hpp"

#include <stdarg.h>  // va_start
#include <stdio.h>
#include <string.h>

// This will delete the file at tempFilename
bool writeIfContentsNewer(const char* tempFilename, const char* outputFilename)
{
	// Read temporary file and destination file and compare
	FILE* newFile = fopen(tempFilename, "rb");
	if (!newFile)
	{
		Logf("error: Could not open %s\n", tempFilename);
		return false;
	}
	FILE* oldFile = fopen(outputFilename, "rb");
	if (!oldFile)
	{
		// Write new and remove temp
		if (logging.fileSystem)
			Log("Destination file didn't exist. Writing\n");

		fclose(newFile);
		return moveFile(tempFilename, outputFilename);
	}
	else
	{
		if (logging.fileSystem)
			Log("Destination file exists. Comparing\n");

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
			if (logging.fileSystem)
				Log("Files are identical. Skipping\n");

			fclose(newFile);
			fclose(oldFile);
			if (remove(tempFilename) != 0)
			{
				Logf("Failed to remove %s\n", tempFilename);
				return false;
			}
			return true;
		}

		if (logging.fileSystem)
			Log("File changed. writing\n");

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
			Log("\nWarning: Use of PascalCase for type names is discouraged because it will "
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
		Logf("\nWarning: Name was given conflicting convert flags: %d\n", (int)modifierFlags);

	return mode;
}

enum WriterOutputScopeType
{
	WriterOutputScope_Normal,
	WriterOutputScope_ContinueBreakable,
};

struct WriterOutputScope
{
	std::vector<const StringOutput*> onScopeExitOutputs;
	WriterOutputScopeType type;
	// Return, continue, or break will cause the scope to have already written out its
	// onScopeExitOutputs. We need to record this so we don't re-write them in the natural
	// ScopeExit.
	bool scopeHasBeenExitedExplicitly;
};

struct StringOutputState
{
	int blockDepth;
	// For determining character offsets to pass to e.g. Emacs' goto-char
	int numCharsOutput;
	int currentLine;
	int lastLineIndented;
	FILE* fileOut;

	std::vector<WriterOutputScope> scopeStack;
};

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
	// Only indent at beginning of line. This will of course break if there are newlines added in
	// strings being output
	if (state.currentLine == state.lastLineIndented)
		return;

	state.lastLineIndented = state.currentLine;

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

static void updateDepthDoIndentation(const WriterFormatSettings& formatSettings,
                                     const StringOutput& outputOperation, StringOutputState& state)
{
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

	printIndentation(formatSettings, state);
}

static void writeStringOutput(const NameStyleSettings& nameSettings,
                              const WriterFormatSettings& formatSettings,
                              const StringOutput& outputOperation, StringOutputState& state)
{
	updateDepthDoIndentation(formatSettings, outputOperation, state);

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
	{
		const char* stringToOutput = outputOperation.output.c_str();
		Writer_Writef(state, "\"");
		char previousChar = 0;
		for (const char* currentChar = stringToOutput; *currentChar; ++currentChar)
		{
			// Escape quotes
			if (*currentChar == '\"' && previousChar != '\\')
			{
				Writer_Writef(state, "\\\"");
			}
			// Handle multiline strings
			else if (*currentChar == '\n')
				Writer_Writef(state, "\\n\"\n\"");
			else
				Writer_Writef(state, "%c", *currentChar);
			previousChar = *currentChar;
		}
		Writer_Writef(state, "\"");
	}
	// Just by changing these we can change the output formatting
	else if (outputOperation.modifiers & StringOutMod_OpenBlock)
	{
		if (formatSettings.uglyPrint)
			Writer_Writef(state, "{");
		else if (formatSettings.braceStyle == WriterFormatBraceStyle_Allman)
		{
			Writer_Writef(state, "\n");
			state.currentLine += 1;

			// Allman brackets are not as deep as their contents, but this bracket was already
			// counted by updateDepthDoIndentation() above. Take it back one so indentation is
			// correct for the bracket, then go back to the proper block indentation
			state.blockDepth -= 1;
			printIndentation(formatSettings, state);
			state.blockDepth += 1;

			Writer_Writef(state, "{\n");
			state.currentLine += 1;
		}
		else if (formatSettings.braceStyle == WriterFormatBraceStyle_KandR_1TBS)
		{
			Writer_Writef(state, " {\n");
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
		if (logging.splices)
		{
			Writer_Writef(state, "; // %s %d %d\n", outputOperation.startToken->source,
			              outputOperation.startToken->lineNumber,
			              outputOperation.startToken->columnStart);
			++state.currentLine;
		}
		else if (formatSettings.uglyPrint)
			Writer_Writef(state, ";");
		else
		{
			Writer_Writef(state, ";\n");
			++state.currentLine;
		}
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

static void writeOutputFollowSplices_Recursive(const NameStyleSettings& nameSettings,
                                               const WriterFormatSettings& formatSettings,
                                               StringOutputState& outputState,
                                               const std::vector<StringOutput>& outputOperations,
                                               bool isHeader);

static void writeSpliceOutput(const NameStyleSettings& nameSettings,
                              const WriterFormatSettings& formatSettings,
                              StringOutputState& outputState, const StringOutput& operation,
                              bool isHeader)
{
	if (operation.spliceOutput)
	{
		// Even if the spliceOutput has output for the other type, we'll get to it when we
		// go to write that type, because all splices must push to both types
		if (isHeader)
		{
			writeOutputFollowSplices_Recursive(nameSettings, formatSettings, outputState,
			                                   operation.spliceOutput->header,
			                                   /*isHeader=*/true);
		}
		else
		{
			if (logging.splices)
				Writer_Writef(outputState, "/* Splice %p { */", operation.spliceOutput);

			writeOutputFollowSplices_Recursive(nameSettings, formatSettings, outputState,
			                                   operation.spliceOutput->source,
			                                   /*isHeader=*/false);

			if (logging.splices)
				Writer_Writef(outputState, "/* Splice %p } */", operation.spliceOutput);
		}
	}
	else
		ErrorAtToken(*operation.startToken, "expected splice output");
}

static void writeOnScopeExit(const NameStyleSettings& nameSettings,
                             const WriterFormatSettings& formatSettings,
                             StringOutputState& outputState, bool isHeader, int scopeIndex)
{
	int numScopeOutputs = 0;
	if (scopeIndex >= 0)
	{
		WriterOutputScope* currentScope = &outputState.scopeStack[scopeIndex];
		numScopeOutputs = currentScope->onScopeExitOutputs.size();
	}
	// We output discrete (defer) invocations in reverse order because they can be dependent
	for (int i = numScopeOutputs - 1; i >= 0; --i)
	{
		// We need to rely on indices because nested scopes could push to the stack
		// Unlikely in a defer, but possible.
		const StringOutput* outputOnScopeExit =
		    outputState.scopeStack[scopeIndex].onScopeExitOutputs[i];
		if (outputOnScopeExit->spliceOutput)
			writeSpliceOutput(nameSettings, formatSettings, outputState, *outputOnScopeExit,
			                  isHeader);
	}
}

static void writeOutputFollowSplices_Recursive(const NameStyleSettings& nameSettings,
                                               const WriterFormatSettings& formatSettings,
                                               StringOutputState& outputState,
                                               const std::vector<StringOutput>& outputOperations,
                                               bool isHeader)
{
	for (const StringOutput& operation : outputOperations)
	{
		// Debug print mapping
		if (!operation.output.empty() && false)
		{
			Logf("%s \t%d\tline %d\n", operation.output.c_str(), outputState.numCharsOutput + 1,
			     outputState.currentLine + 1);
		}

		// Handle scope-related splicing
		// Scopes don't ever make sense in headers
		if (!isHeader)
		{
			if (operation.modifiers & StringOutMod_ScopeEnter)
			{
				if (logging.scopes)
					NoteAtToken(*operation.startToken, "Enter scope");
				WriterOutputScope newScope;
				newScope.type = WriterOutputScope_Normal;
				newScope.scopeHasBeenExitedExplicitly = false;
				outputState.scopeStack.push_back(newScope);
				if (logging.scopes)
					Writer_Writef(outputState, "/* Enter scope %d { */",
					              outputState.scopeStack.size());
			}
			else if (operation.modifiers & StringOutMod_ScopeContinueBreakableEnter)
			{
				if (logging.scopes)
					NoteAtToken(*operation.startToken, "Enter continue breakable scope");
				WriterOutputScope newScope;
				newScope.type = WriterOutputScope_ContinueBreakable;
				newScope.scopeHasBeenExitedExplicitly = false;
				outputState.scopeStack.push_back(newScope);
				if (logging.scopes)
					Writer_Writef(outputState, "/* Enter continue breakable scope %d { */",
					              outputState.scopeStack.size());
			}

			if (operation.modifiers & StringOutMod_SpliceOnScopeExit)
			{
				if (outputState.scopeStack.empty())
					ErrorAtToken(*operation.startToken,
					             "Scope stack is invalid when trying to add splice. This is likely "
					             "an error with Cakelisp itself, or a generator which isn't "
					             "opening and closing scopes as expected.");
				else
				{
					WriterOutputScope* currentScope =
					    &outputState.scopeStack[(outputState.scopeStack.size() - 1)];
					currentScope->onScopeExitOutputs.push_back(&operation);
				}
			}

			if (logging.scopes)
			{
				if (operation.modifiers & StringOutMod_ScopeExit ||
				    operation.modifiers & StringOutMod_ScopeContinueBreakableExit)
				{
					if (logging.scopes)
						NoteAtToken(*operation.startToken, "Exit scope");
					Writer_Writef(outputState, "/* Exit scope %d } */",
					              outputState.scopeStack.size());
				}
			}

			if (operation.modifiers & StringOutMod_ScopeExit ||
			    // Do we need this?
			    operation.modifiers & StringOutMod_ScopeContinueBreakableExit)
			{
				int currentScopeIndex = (outputState.scopeStack.size() - 1);
				if (!outputState.scopeStack.empty() &&
				    !outputState.scopeStack[currentScopeIndex].scopeHasBeenExitedExplicitly)
					writeOnScopeExit(nameSettings, formatSettings, outputState, isHeader,
					                 currentScopeIndex);

				if (!outputState.scopeStack.empty())
					outputState.scopeStack.pop_back();
				else
					ErrorAtToken(*operation.startToken,
					             "Scope stack is invalid on scope exit. This is likely an error "
					             "with Cakelisp itself, or a generator which isn't opening and "
					             "closing scopes as expected.");
			}

			if (operation.modifiers & StringOutMod_ScopeContinueOrBreak)
			{
				if (logging.scopes)
					NoteAtToken(*operation.startToken, "Continue or break");
				int currentScopeIndex = (outputState.scopeStack.size() - 1);
				if (!outputState.scopeStack.empty())
					outputState.scopeStack[currentScopeIndex].scopeHasBeenExitedExplicitly = true;
				for (int i = currentScopeIndex; i >= 0; --i)
				{
					writeOnScopeExit(nameSettings, formatSettings, outputState, isHeader, i);
					// Stop at the first for/while/etc. we find
					if (outputState.scopeStack[i].type == WriterOutputScope_ContinueBreakable)
						break;
				}
				// We don't pop the scope because this is an unusual control flow exit
			}

			if (operation.modifiers & StringOutMod_ScopeExitAll)
			{
				int currentScopeIndex = (outputState.scopeStack.size() - 1);
				if (!outputState.scopeStack.empty())
					outputState.scopeStack[currentScopeIndex].scopeHasBeenExitedExplicitly = true;
				for (int i = (outputState.scopeStack.size() - 1); i >= 0; --i)
				{
					writeOnScopeExit(nameSettings, formatSettings, outputState, isHeader, i);
				}
				// We don't pop the scope because this is an unusual control flow exit
			}
		}

		// Only plain old splices are output this way
		if (operation.modifiers == StringOutMod_Splice)
		{
			writeSpliceOutput(nameSettings, formatSettings, outputState, operation, isHeader);
		}
		else
			writeStringOutput(nameSettings, formatSettings, operation, outputState);
	}
}

bool writeOutputs(const NameStyleSettings& nameSettings, const WriterFormatSettings& formatSettings,
                  const WriterOutputSettings& outputSettings, const GeneratorOutput& outputToWrite)
{
	struct
	{
		bool isHeader;
		const char* outputFilename;

		StringOutputState outputState;
		// To determine if anything was actually written
		StringOutputState stateBeforeOutputWrite;
		char tempFilename[MAX_PATH_LENGTH];
	} outputs[] = {{/*isHeader=*/false, outputSettings.sourceOutputName, {}, {}, {0}},
	               {/*isHeader=*/true, outputSettings.headerOutputName, {}, {}, {0}}};

	for (int i = 0; i < static_cast<int>(ArraySize(outputs)); ++i)
	{
		if (!outputs[i].outputFilename)
			continue;

		// Write to a temporary file
		PrintfBuffer(outputs[i].tempFilename, "%s.temp", outputs[i].outputFilename);
		// TODO: If this fails to open, Writer_Writef just won't write to the file, it'll print
		outputs[i].outputState.fileOut = fileOpen(outputs[i].tempFilename, "wb");

		if (outputSettings.heading)
		{
			writeOutputFollowSplices_Recursive(nameSettings, formatSettings, outputs[i].outputState,
			                                   outputs[i].isHeader ?
			                                       outputSettings.heading->header :
			                                       outputSettings.heading->source,
			                                   outputs[i].isHeader);
		}

		// if (outputs[i].heading)
		// {
		// 	Writer_Writef(outputs[i].outputState, outputs[i].heading);
		// 	// TODO: Put this in Writer_Writef
		// 	for (const char* c = outputs[i].heading; *c != '\0'; ++c)
		// 	{
		// 		if (*c == '\n')
		// 			++outputs[i].outputState.currentLine;
		// 	}
		// }

		outputs[i].stateBeforeOutputWrite = outputs[i].outputState;

		// Write the output!
		writeOutputFollowSplices_Recursive(
		    nameSettings, formatSettings, outputs[i].outputState,
		    outputs[i].isHeader ? outputToWrite.header : outputToWrite.source, outputs[i].isHeader);

		// No output to this file. Don't write anything
		if (outputs[i].outputState.numCharsOutput ==
		    outputs[i].stateBeforeOutputWrite.numCharsOutput)
		{
			if (logging.fileSystem)
				Logf("%s had no meaningful output\n", outputs[i].tempFilename);

			fclose(outputs[i].outputState.fileOut);
			outputs[i].outputState.fileOut = nullptr;

			if (remove(outputs[i].tempFilename) != 0)
			{
				Logf("Error: Failed to remove %s\n", outputs[i].tempFilename);
				return false;
			}
			continue;
		}

		if (outputSettings.footer)
		{
			writeOutputFollowSplices_Recursive(
			    nameSettings, formatSettings, outputs[i].outputState,
			    outputs[i].isHeader ? outputSettings.footer->header : outputSettings.footer->source,
			    outputs[i].isHeader);
		}

		// if (outputs[i].footer)
		// {
		// 	Writer_Writef(outputs[i].outputState, outputs[i].footer);
		// 	// TODO: Put this in Writer_Writef
		// 	for (const char* c = outputs[i].footer; *c != '\0'; ++c)
		// 	{
		// 		if (*c == '\n')
		// 			++outputs[i].outputState.currentLine;
		// 	}
		// }

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
	if (!writeOutputs(nameSettings, formatSettings, outputSettings, generatedOutput))
		return false;

	// TODO: Write mapping and metadata
	if (logging.metadata)
	{
		// Metadata
		Log("\n\tImports:\n");
		for (const ImportMetadata& import : generatedOutput.imports)
		{
			Logf("%s\t(%s)\n", import.importName.c_str(), importLanguageToString(import.language));
		}

		Log("\n\tFunctions:\n");
		for (const FunctionMetadata& function : generatedOutput.functions)
		{
			Logf("%s\n", function.name.c_str());
			if (!function.arguments.empty())
			{
				Log("(\n");
				for (const FunctionArgumentMetadata& argument : function.arguments)
				{
					Logf("\t%s\n", argument.name.c_str());
				}
				Log(")\n");
			}
			else
				Log("()\n");

			Log("\n");
		}
	}

	return true;
}

bool writeCombinedHeader(const char* combinedHeaderFilename,
                         std::vector<const char*>& headersToInclude)
{
	char tempFilename[MAX_PATH_LENGTH] = {0};
	PrintfBuffer(tempFilename, "%s.temp", combinedHeaderFilename);

	FILE* combinedHeaderFile = fopen(tempFilename, "wb");
	if (!combinedHeaderFile)
	{
		perror("fopen: ");
		Logf("error: failed open %s", tempFilename);
		return false;
	}

	// G++ complains if there's one of these in the "main" file. When we precompile, the header is
	// always the main file
	// fprintf(combinedHeaderFile, "#pragma once\n");

	for (const char* sourceHeader : headersToInclude)
	{
		fprintf(combinedHeaderFile, "#include \"%s\"\n", sourceHeader);
	}

	fclose(combinedHeaderFile);

	if (!writeIfContentsNewer(tempFilename, combinedHeaderFilename))
		return false;

	return true;
}
