#include <stdio.h>

#ifdef WINDOWS
#define DEPENDENCY_API __declspec(dllexport)
#else
#define DEPENDENCY_API
#endif

DEPENDENCY_API void test()
{
	printf("Hello from C!\n");
}
