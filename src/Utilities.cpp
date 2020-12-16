#include "Utilities.hpp"

#include <stdio.h>

#include "Logging.hpp"

std::string EmptyString;

void printIndentToDepth(int depth)
{
	for (int i = 0; i < depth; ++i)
		Log("\t");
}

FILE* fileOpen(const char* filename, const char* mode)
{
	FILE* file = nullptr;
	file = fopen(filename, mode);
	if (!file)
	{
		Logf("error: Could not open %s\n", filename);
		return nullptr;
	}
	else
	{
		if (log.fileSystem)
			Logf("Opened %s\n", filename);
	}
	return file;
}

bool writeCharToBuffer(char c, char** at, char* bufferStart, int bufferSize)
{
	**at = c;
	++(*at);
	char* endOfBuffer = bufferStart + (bufferSize - 1);
	if (*at > endOfBuffer)
	{
		Logf("error: buffer of size %d was too small. String will be cut off\n", bufferSize);
		*endOfBuffer = '\0';
		return false;
	}

	return true;
}

bool writeStringToBuffer(const char* str, char** at, char* bufferStart, int bufferSize)
{
	for (const char* c = str; *c != '\0'; ++c)
	{
		**at = *c;
		++(*at);
		char* endOfBuffer = bufferStart + (bufferSize - 1);
		if (*at > endOfBuffer)
		{
			Logf("error: buffer of size %d was too small. String will be cut off\n", bufferSize);
			*(endOfBuffer) = '\0';
			return false;
		}
	}

	return true;
}

// From http://home.thep.lu.se/~bjorn/crc/ (public domain)
uint32_t crc32_for_byte(uint32_t r)
{
	for (int j = 0; j < 8; ++j)
		r = (r & 1 ? 0 : (uint32_t)0xEDB88320L) ^ r >> 1;
	return r ^ (uint32_t)0xFF000000L;
}

void crc32(const void* data, size_t n_bytes, uint32_t* crc)
{
	static uint32_t table[0x100];
	if (!*table)
		for (size_t i = 0; i < 0x100; ++i)
			table[i] = crc32_for_byte(i);
	for (size_t i = 0; i < n_bytes; ++i)
		*crc = table[(uint8_t)*crc ^ ((uint8_t*)data)[i]] ^ *crc >> 8;
}
