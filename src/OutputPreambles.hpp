#pragma once

struct GeneratorOutput;
struct Token;

void makeCompileTimeHeaderFooter(GeneratorOutput& headerOut, GeneratorOutput& footerOut,
                                 GeneratorOutput* spliceAfterHeaders, const Token* blameToken);
void makeRunTimeHeaderFooter(GeneratorOutput& headerOut, GeneratorOutput& footerOut,
                             const Token* blameToken);
