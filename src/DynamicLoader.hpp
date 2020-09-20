#pragma once

typedef void* DynamicLibHandle;

DynamicLibHandle loadDynamicLibrary(const char* libraryPath);

void* getSymbolFromDynamicLibrary(DynamicLibHandle library, const char* symbolName);

void closeAllDynamicLibraries();
void closeDynamicLibrary(DynamicLibHandle library);
