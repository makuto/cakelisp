#include "DynamicLoader.hpp"

#include <stdio.h>

#ifdef UNIX
#include <dlfcn.h>
#elif WINDOWS
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#else
#error Platform support is needed for dynamic loading
#endif

DynamicLibHandle loadDynamicLibrary(const char* libraryPath)
{
	void* libHandle = nullptr;

#ifdef UNIX
	// RTLD_LAZY: Don't look up symbols the shared library needs until it encounters them
	// Note that this requires linking with -Wl,-rpath,. in order to turn up relative path .so files
	libHandle = dlopen(libraryPath, RTLD_LAZY);

	if (!libHandle)
	{
		fprintf(stderr, "DynamicLoader Error:\n%s\n", dlerror());
		return nullptr;
	}
#elif WINDOWS
	// TODO: Any way to get errors if this fails?
	libHandle = LoadLibrary(libraryPath);
#endif

	return libHandle;
}

void* getSymbolFromDynamicLibrary(DynamicLibHandle library, const char* symbolName)
{
	if (!library)
	{
		fprintf(stderr, "DynamicLoader Error: Received empty library handle\n");
		return nullptr;
	}

#ifdef UNIX
	// Clear any existing error before running dlsym
	char* error = dlerror();
	if (error != nullptr)
	{
		fprintf(stderr, "DynamicLoader Error:\n%s\n", error);
		return nullptr;
	}

	void* symbol = dlsym(library, symbolName);

	error = dlerror();
	if (error != nullptr)
	{
		fprintf(stderr, "DynamicLoader Error:\n%s\n", error);
		return nullptr;
	}

	return symbol;
#elif WINDOWS
	// TODO: Any way to get errors if this fails?
	// Sergey: GetLastError
	void* procedure = (void*)GetProcAddress((HINSTANCE)library, symbolName);
	return procedure;
#else
	return nullptr;
#endif
}
