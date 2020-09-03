#include "DynamicLoader.hpp"

#include <stdio.h>

#ifdef UNIX
#include <dlfcn.h>
#endif

extern "C"
{
	int hostSquareFunc(int a)
	{
		return a * a;
	}
}

int main()
{
#ifdef UNIX
	void* libHandle;
	// libHandle = dlopen("./libSquare.so", RTLD_LAZY);
	libHandle =
	    dlopen("/home/macoy/Development/code/repositories/cakelisp/src/libSquare.so", RTLD_LAZY);
	if (!libHandle)
	{
		fprintf(stderr, "DynamicLoader Error:\n%s\n", dlerror());
		return 1;
	}

	// Clear any existing error
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
	printf("%f\n", (*square)(2.0));
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
