#ifdef WINDOWS
#ifdef CAKELISP_EXPORTING
#define CAKELISP_API __declspec(dllexport)
#else
#define CAKELISP_API __declspec(dllimport)
#endif
#else
#define CAKELISP_API
#endif
