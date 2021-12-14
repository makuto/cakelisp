#pragma once


enum ModuleDependencyType
{
	ModuleDependency_Cakelisp,
	ModuleDependency_Library,
	ModuleDependency_CFile
};

enum CakelispImportOutput
{
	CakelispImportOutput_Header = 1 << 0,
	CakelispImportOutput_Source = 1 << 1,
	CakelispImportOutput_Both = CakelispImportOutput_Header | CakelispImportOutput_Source,
};
