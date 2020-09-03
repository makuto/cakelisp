#include "DynamicLoader.hpp"

// clang++ -shared -o libSquare.so  Square.o

extern "C"
{
	float square(float numToSquare)
	{
		// return numToSquare * numToSquare;
		return hostSquareFunc((int)numToSquare);
	}
}
