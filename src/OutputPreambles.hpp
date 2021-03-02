#pragma once

struct GeneratorOutput;
struct Token;

// If comptimeCombinedHeaderFilename is null, g_comptimeDefaultHeaders will be inserted instead
void makeCompileTimeHeaderFooter(GeneratorOutput& headerOut, GeneratorOutput& footerOut,
                                 const char* comptimeCombinedHeaderFilename,
                                 GeneratorOutput* spliceAfterHeaders, const Token* blameToken);
void makeRunTimeHeaderFooter(GeneratorOutput& headerOut, GeneratorOutput& footerOut,
                             const Token* blameToken);

extern const char* g_comptimeDefaultHeaders[10];
