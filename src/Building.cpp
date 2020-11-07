#include "Building.hpp"

#include "Utilities.hpp"
#include "FileUtilities.hpp"

bool objectFilenameFromSourceFilename(const char* cakelispWorkingDir, const char* sourceFilename,
                                      char* bufferOut, int bufferSize)
{
	char buildFilename[MAX_NAME_LENGTH] = {0};
	getFilenameFromPath(sourceFilename, buildFilename, sizeof(buildFilename));
	if (!buildFilename[0])
		return false;

	// TODO: Trim .cake.cpp (etc.)
	SafeSnprinf(bufferOut, bufferSize, "%s/%s.o", cakelispWorkingDir, buildFilename);
	return true;
}
