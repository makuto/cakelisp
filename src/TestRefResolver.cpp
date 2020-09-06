#include <vector>
#include <unordered_map>
#include <stdio.h>

#include "Utilities.hpp"

// Testing only
enum ThingType
{
	Type_Definition,
	Type_Reference
};

struct Thing
{
	ThingType type;
	bool isMacro;
	std::string name;
	// For definitions only
	std::vector<Thing> body;
	std::vector<Thing> afterBuildAdd;
	bool isModuleRequired;
};

// Loosely resembles what will be necessary

// TODO
// struct DefRefStatus
// {
// status = Done, WaitingForChange, NeverChecked
// WaitingForChange means don't recompile me until there has been some ref with a change (how to know which?)
// };

struct Definition
{
	const Thing* thing;
	std::string name;
	bool isModuleRequired;
	bool isMacro;
	std::vector<std::string> references;
	std::vector<Thing> afterBuildAdd;
};

struct Reference
{
	std::string name;
	bool isModuleRequired;
	std::vector<std::string> referents;
};

std::unordered_map<std::string, Definition> definitions;
std::unordered_map<std::string, Reference> references;
std::unordered_map<std::string, int> loadedCode;

// Reads environment
bool willCompileSucceed(const std::string& thing)
{
	// If we try to compile with missing macro (defined by MacroA causing MacroB to create MacroC)
	if (thing.compare("FuncA") == 0 && loadedCode.find("MacroC") == loadedCode.end())
		return false;
	else if (thing.compare("MacroD") == 0 && loadedCode.find("MacroC") == loadedCode.end())
		return false;

	return true;
}

void Evaluate(const Thing& thing, const Thing* parent)
{
	if (thing.type == Type_Definition)
	{
		Definition newDefinition = {&thing,        thing.name, thing.isModuleRequired,
		                            thing.isMacro, {},         thing.afterBuildAdd};
		definitions[thing.name] = newDefinition;
		printf("%s defined required = %s\n", thing.name.c_str(), thing.isModuleRequired ? "yes" : "no");

		for (const Thing& child : thing.body)
			Evaluate(child, &thing);
	}
	else
	{
		if (!parent)
			printf("Error: expected references only with parent");
		else
		{
			// TODO: This should be a unique push
			definitions[parent->name].references.push_back(thing.name);
			// TODO: Append to referents if not in list yet
			Reference newReference = {thing.name, parent->isModuleRequired, {parent->name}};
			references[thing.name] = newReference;
		}
	}
}

int main()
{
	// Ever heard of an enum
	bool isMacro = true;
	bool isntMacro = false;
	bool isModuleRequired = true;
	bool notModuleRequired = false;
	Thing moduleContents[] = {{Type_Definition,
	                           isntMacro,
	                           "FuncA",
	                           {
	                               {Type_Reference, isntMacro, "MacroA", {}, {}, notModuleRequired},
	                               {Type_Reference, isntMacro, "MacroC", {}, {}, notModuleRequired},
	                               {Type_Reference, isntMacro, "MacroD", {}, {}, notModuleRequired},
	                           },
	                           {},
	                           isModuleRequired},
	                          {Type_Definition,
	                           isMacro,
	                           "MacroA",
	                           {{Type_Reference, isntMacro, "CFuncA", {}, {}, notModuleRequired},
	                            // TODO Need after load for this
	                            {Type_Reference, isntMacro, "MacroB", {}, {}, notModuleRequired}},
	                           // Once loading MacroA, create MacroC
	                           {{Type_Definition, isMacro, "MacroC", {}, {}, notModuleRequired}},
	                           notModuleRequired},
	                          {Type_Definition, isMacro, "MacroB", {}, {}, notModuleRequired},
	                          {Type_Definition,
	                           isMacro,
	                           "MacroD",
	                           {{Type_Reference, isntMacro, "MacroC", {}, {}, notModuleRequired}},
	                           {},
	                           notModuleRequired}};

	for (int i = 0; i < static_cast<int>(ArraySize(moduleContents)); ++i)
	{
		// Module is the only thing which pass nullptr for parent
		Evaluate(moduleContents[i], nullptr);
	}


	// Dear lord
	int numReferencesResolved = 0;
	do
	{
		numReferencesResolved = 0;

		printf("\nPropagate requires to %lu definitions\n\n", definitions.size());

		// Figure out what is required
		// This needs to loop so long as it doesn't recurse to references
		int numRequiresStatusChanged = 0;
		do
		{
			numRequiresStatusChanged = 0;
			for (const std::pair<const std::string, Definition>& definition : definitions)
			{
				const char* status =
				    definition.second.isModuleRequired ? "(required)" : "(not required)";
				printf("Define %s %s\n", definition.second.name.c_str(), status);

				for (const std::string& reference : definition.second.references)
				{
					printf("\tRefers to %s\n", reference.c_str());
					if ((definition.second.isModuleRequired ||
					     references[reference].isModuleRequired) &&
					    definitions.find(reference) != definitions.end())
					{
						if (!definitions[reference].isModuleRequired)
						{
							printf("\t Infecting %s with required due to %s\n", reference.c_str(),
							       definition.second.name.c_str());
							++numRequiresStatusChanged;
							definitions[reference].isModuleRequired = true;
						}
						// TODO Recurse, infecting all of the definition's references as required?
					}
				}
			}
		} while (numRequiresStatusChanged);

		printf("\nBuild\n\n");

		int numBuilt = 0;
		int numIterations = 1;
		do
		{
			printf("Build Iteration %d\n", numIterations++);
			numBuilt = 0;
			// Is it possible to get around this?
			std::unordered_map<std::string, Definition> definitionsCopy = definitions;
			for (const std::pair<const std::string, Definition>& definition : definitionsCopy)
			{
				// Already loaded
				if (loadedCode.find(definition.second.name) != loadedCode.end())
					continue;

				const char* status =
				    definition.second.isModuleRequired ? "(required)" : "(not required)";
				printf("Definition %s %s\n", definition.second.name.c_str(), status);

				if (!definition.second.isModuleRequired)
					continue;

				bool canBuild = true;
				bool missingReferences = false;
				for (const std::string& referenceName : definition.second.references)
				{
					if (definitions.find(referenceName) != definitions.end())
					{
						if (definitions[referenceName].isMacro)
						{
							if (loadedCode.find(referenceName) == loadedCode.end())
							{
								printf("\tCannot build until %s is loaded\n",
								       referenceName.c_str());
								missingReferences = true;
								canBuild = false;
							}
							else
							{
								printf("\tInvoking %s\n", referenceName.c_str());
								if (definition.second.name.compare("MacroA") == 0)
								{
									printf("*** After evaluation, added MacroC ***\n");
									Evaluate(definition.second.afterBuildAdd[0],
									         definition.second.thing);
								}
							}
						}
					}
					else
					{
						printf("\tWill need to guess that %s is a C function\n",
						       referenceName.c_str());
					}
				}

				if (canBuild)
				{
					bool didCompile = willCompileSucceed(definition.second.name);
					printf("\tBuilding %s : %s\n", definition.second.name.c_str(),
					       didCompile ? "Success" : "Failure");
					if (didCompile)
					{
						loadedCode[definition.second.name] = 1;

						++numBuilt;
						++numReferencesResolved;
					}
				}
			}
		} while (numBuilt);
	} while (numReferencesResolved);

	printf("\nResults\n\n");

	int errors = 0;
	for (const std::pair<const std::string, Definition>& definition : definitions)
	{
		printf("%s:\n", definition.second.name.c_str());
		if (definition.second.isModuleRequired)
		{
			if (loadedCode.find(definition.second.name) == loadedCode.end())
			{
				printf("\tCould NOT build required\n");
				++errors;
			}
			else
				printf("\tBuilt successfully\n");
		}
		else
		{
			printf("\tOmitted (not required by module)\n");
		}
	}

	return errors;
}
