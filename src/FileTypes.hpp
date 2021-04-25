#pragma once

#include <stdint.h> // int64_t

#ifdef WINDOWS
#define FORMAT_FILETIME "%I64u"
#else
#define FORMAT_FILETIME "%lu"
#endif

typedef int64_t FileModifyTime;
