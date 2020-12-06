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

	bool fileSystem;
	bool fileSearch;

	bool metadata;
	bool processes;

	bool imports;
	bool phases;
	bool performance;
};

extern LoggingSettings log;
