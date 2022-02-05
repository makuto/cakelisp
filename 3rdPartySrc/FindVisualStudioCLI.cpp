#include "FindVisualStudio.hpp"

int main()
{
	Find_Result result = find_visual_studio_and_windows_sdk();
	free_resources(&result);
	return 0;
}
