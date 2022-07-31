#pragma once

#include "Exporting.hpp"

struct LoggingSettings
{
	// verbosity
	bool tokenization;

	bool references;
	bool dependencyPropagation;
	bool buildReasons;
	bool buildProcess;
	bool buildOmissions;
	bool compileTimeBuildObjects;
	bool compileTimeBuildReasons;
	bool commandCrcs;
	bool compileTimeHooks;
	bool splices;
	bool scopes;

	bool fileSystem;
	bool fileSearch;

	bool metadata;
	bool processes;

	bool imports;
	bool phases;
	bool performance;
	bool includeScanning;
	bool strictIncludes;
	bool optionAdding;
};

extern CAKELISP_API LoggingSettings logging;
