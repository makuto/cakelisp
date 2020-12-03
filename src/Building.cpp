#include "Building.hpp"

#include "Utilities.hpp"
#include "FileUtilities.hpp"

bool outputFilenameFromSourceFilename(const char* outputDir, const char* sourceFilename,
                                      const char* addExtension, char* bufferOut, int bufferSize)
{
	char buildFilename[MAX_NAME_LENGTH] = {0};
	getFilenameFromPath(sourceFilename, buildFilename, sizeof(buildFilename));
	if (!buildFilename[0])
		return false;

	// TODO: Trim .cake.cpp (etc.)
	if (!addExtension)
	{
		SafeSnprinf(bufferOut, bufferSize, "%s/%s", outputDir, buildFilename);
	}
	else
	{
		SafeSnprinf(bufferOut, bufferSize, "%s/%s.%s", outputDir, buildFilename, addExtension);
	}
	return true;
}
