/*
 * File: framework\src\hosc_modules.c
 * Purpose: HOSC source file.
 */

#include "hosc_runtime.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

#ifdef _WIN32
#include <windows.h>
#endif

// ============================================================================
// CORE MODULE
// ============================================================================

static void* core_module_init(void) {
    printf("core module initialized\n");
    return NULL;
}

static void core_module_cleanup(void* context) {
    (void)context;
    printf("core module cleaned up\n");
}

static void* core_module_get_function(const char* function_name) {
    if (strcmp(function_name, "print") == 0) {
        return (void*)printf;
    }
    return NULL;
}

static bool core_module_is_loaded(void) {
    return true;
}

HOSCModule hosc_core_module = {
    .name = "core",
    .version = "1.0.0",
    .init = core_module_init,
    .cleanup = core_module_cleanup,
    .get_function = core_module_get_function,
    .is_loaded = core_module_is_loaded
};

// ============================================================================
// IO MODULE
// ============================================================================

static void* io_module_init(void) {
    printf("io module initialized\n");
    return NULL;
}

static void io_module_cleanup(void* context) {
    (void)context;
    printf("io module cleaned up\n");
}

static void* io_module_get_function(const char* function_name) {
    (void)function_name;
    return NULL;
}

static bool io_module_is_loaded(void) {
    return true;
}

HOSCModule hosc_io_module = {
    .name = "io",
    .version = "1.0.0",
    .init = io_module_init,
    .cleanup = io_module_cleanup,
    .get_function = io_module_get_function,
    .is_loaded = io_module_is_loaded
};

// ============================================================================
// MATH MODULE
// ============================================================================

static void* math_module_init(void) {
    printf("math module initialized\n");
    return NULL;
}

static void math_module_cleanup(void* context) {
    (void)context;
    printf("math module cleaned up\n");
}

static void* math_module_get_function(const char* function_name) {
    if (strcmp(function_name, "sin") == 0) {
        return (void*)sin;
    }
    if (strcmp(function_name, "cos") == 0) {
        return (void*)cos;
    }
    if (strcmp(function_name, "sqrt") == 0) {
        return (void*)sqrt;
    }
    if (strcmp(function_name, "pow") == 0) {
        return (void*)pow;
    }
    return NULL;
}

static bool math_module_is_loaded(void) {
    return true;
}

HOSCModule hosc_math_module = {
    .name = "math",
    .version = "1.0.0",
    .init = math_module_init,
    .cleanup = math_module_cleanup,
    .get_function = math_module_get_function,
    .is_loaded = math_module_is_loaded
};

// ============================================================================
// STRING MODULE
// ============================================================================

static void* string_module_init(void) {
    printf("string module initialized\n");
    return NULL;
}

static void string_module_cleanup(void* context) {
    (void)context;
    printf("string module cleaned up\n");
}

static void* string_module_get_function(const char* function_name) {
    if (strcmp(function_name, "strlen") == 0) {
        return (void*)strlen;
    }
    if (strcmp(function_name, "strcmp") == 0) {
        return (void*)strcmp;
    }
    return NULL;
}

static bool string_module_is_loaded(void) {
    return true;
}

HOSCModule hosc_string_module = {
    .name = "string",
    .version = "1.0.0",
    .init = string_module_init,
    .cleanup = string_module_cleanup,
    .get_function = string_module_get_function,
    .is_loaded = string_module_is_loaded
};

// ============================================================================
// WIN32 MODULE
// ============================================================================

static void* win32_module_init(void) {
#ifdef _WIN32
    printf("win32 module initialized\n");
#else
    printf("win32 module unavailable on this platform\n");
#endif
    return NULL;
}

static void win32_module_cleanup(void* context) {
    (void)context;
}

static void* win32_module_get_function(const char* function_name) {
#ifdef _WIN32
    if (strcmp(function_name, "MessageBox") == 0) {
        return (void*)MessageBoxA;
    }
    if (strcmp(function_name, "Sleep") == 0) {
        return (void*)Sleep;
    }
    if (strcmp(function_name, "Beep") == 0) {
        return (void*)Beep;
    }
#endif
    (void)function_name;
    return NULL;
}

static bool win32_module_is_loaded(void) {
#ifdef _WIN32
    return true;
#else
    return false;
#endif
}

HOSCModule hosc_win32_module = {
    .name = "win32",
    .version = "1.0.0",
    .init = win32_module_init,
    .cleanup = win32_module_cleanup,
    .get_function = win32_module_get_function,
    .is_loaded = win32_module_is_loaded
};

// ============================================================================
// GUI MODULE
// ============================================================================

static void* gui_module_init(void) {
    hosc_gui_init();
    printf("gui module initialized (%s)\n", hosc_gui_backend_name());
    return NULL;
}

static void gui_module_cleanup(void* context) {
    (void)context;
}

static void* gui_module_get_function(const char* function_name) {
    if (strcmp(function_name, "window") == 0) {
        return (void*)hosc_gui_create_window;
    }
    if (strcmp(function_name, "text") == 0) {
        return (void*)hosc_gui_draw_text;
    }
    if (strcmp(function_name, "pump") == 0) {
        return (void*)hosc_gui_pump_events;
    }
    if (strcmp(function_name, "poll_event") == 0) {
        return (void*)hosc_gui_poll_event;
    }
    if (strcmp(function_name, "is_running") == 0) {
        return (void*)hosc_gui_is_running;
    }
    return NULL;
}

static bool gui_module_is_loaded(void) {
    return true;
}

HOSCModule hosc_gui_module = {
    .name = "gui",
    .version = "1.0.0",
    .init = gui_module_init,
    .cleanup = gui_module_cleanup,
    .get_function = gui_module_get_function,
    .is_loaded = gui_module_is_loaded
};

