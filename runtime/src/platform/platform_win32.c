#include "hosc_runtime_api.h"

const char* hosc_platform_name(void) {
#if defined(_WIN32)
    return "win32";
#elif defined(__APPLE__)
    return "macos";
#elif defined(__linux__)
    return "linux";
#elif defined(__unix__)
    return "unix";
#else
    return "unknown";
#endif
}
