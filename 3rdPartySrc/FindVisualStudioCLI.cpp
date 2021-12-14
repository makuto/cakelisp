struct Find_Result
{
	int windows_sdk_version;  // Zero if no Windows SDK found.

	wchar_t* windows_sdk_root = NULL;
	wchar_t* windows_sdk_um_library_path = NULL;
	wchar_t* windows_sdk_ucrt_library_path = NULL;

	wchar_t* vs_exe_path = NULL;
	wchar_t* vs_library_path = NULL;
};

Find_Result find_visual_studio_and_windows_sdk();

int main()
{
	Find_Result result = find_visual_studio_and_windows_sdk();
	free_resources(&result);
	return 0;
}
