#include "DynamicLoader.hpp"

#include <stdio.h>

#ifdef UNIX
#include <dlfcn.h>
#endif

extern "C"
{
	float hostSquareFunc(float numToSquare)
	{
		return numToSquare * numToSquare;
	}
}

int main()
{
#ifdef UNIX
	void* libHandle;

	// RTLD_LAZY: Don't look up symbols the shared library needs until it encounters them
	// Note that this requires linking with -Wl,-rpath,. in order to turn up relative path .so files
	libHandle = dlopen("src/libSquare.so", RTLD_LAZY);

	if (!libHandle)
	{
		fprintf(stderr, "DynamicLoader Error:\n%s\n", dlerror());
		return 1;
	}

	// Clear any existing error before running dlsym
	char* error = dlerror();
	if (error != nullptr)
	{
		fprintf(stderr, "DynamicLoader Error:\n%s\n", error);
		return 1;
	}

	printf("About to load\n");
	float (*square)(float) = (float (*)(float))dlsym(libHandle, "square");
	printf("Done load\n");

	error = dlerror();
	if (error != nullptr)
	{
		fprintf(stderr, "DynamicLoader Error:\n%s\n", error);
		return 1;
	}

	printf("About to call\n");
	printf("%f\n", (*square)(2.f));
	printf("Done call\n");
	dlclose(libHandle);

	error = dlerror();
	if (error != nullptr)
	{
		fprintf(stderr, "DynamicLoader Error:\n%s\n", error);
		return 1;
	}

	return 0;
#else
	return 1;
#endif
}
