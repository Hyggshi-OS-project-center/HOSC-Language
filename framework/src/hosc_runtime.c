#include "hosc_runtime.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#ifdef _WIN32
#include <windows.h>
#endif

// ============================================================================
// INTERNAL UTILITIES
// ============================================================================

static uint64_t hosc_now_ms(void) {
#ifdef _WIN32
    return (uint64_t)GetTickCount64();
#else
    return (uint64_t)time(NULL) * 1000ULL;
#endif
}

static char* hosc_strdup_local(const char* input) {
    size_t len;
    char* out;

    if (!input) {
        return NULL;
    }

    len = strlen(input);
    out = (char*)malloc(len + 1);
    if (!out) {
        return NULL;
    }

    memcpy(out, input, len + 1);
    return out;
}

// ============================================================================
// GLOBAL RUNTIME CONTEXT
// ============================================================================

static HOSCRuntimeContext* g_runtime_context = NULL;
static uint64_t g_runtime_counter = 0;

// ============================================================================
// MEMORY MANAGEMENT
// ============================================================================

typedef struct {
    size_t total_allocated;
    size_t total_deallocated;
    size_t current_in_use;
    size_t peak_usage;
    size_t allocation_count;
    bool tracking_enabled;
} MemoryStats;

static MemoryStats g_memory_stats = {0};

static size_t memory_header_size(void) {
    return sizeof(size_t);
}

static void memory_track_alloc(size_t sz) {
    g_memory_stats.total_allocated += sz;
    g_memory_stats.current_in_use += sz;
    if (g_memory_stats.current_in_use > g_memory_stats.peak_usage) {
        g_memory_stats.peak_usage = g_memory_stats.current_in_use;
    }
    g_memory_stats.allocation_count++;
}

static void memory_track_free(size_t sz) {
    g_memory_stats.total_deallocated += sz;
    if (g_memory_stats.current_in_use >= sz) {
        g_memory_stats.current_in_use -= sz;
    } else {
        g_memory_stats.current_in_use = 0;
    }
}

static void* memory_allocate(size_t size) {
    size_t header = memory_header_size();
    uint8_t* raw = (uint8_t*)malloc(header + size);
    if (!raw) {
        return NULL;
    }
    memcpy(raw, &size, sizeof(size));

    if (g_memory_stats.tracking_enabled) {
        memory_track_alloc(size);
    }
    return raw + header;
}

static void* memory_reallocate(void* ptr, size_t new_size) {
    size_t header = memory_header_size();
    size_t old_size = 0;
    uint8_t* raw_ptr;
    uint8_t* new_raw;

    if (!ptr) {
        return memory_allocate(new_size);
    }

    raw_ptr = ((uint8_t*)ptr) - header;
    memcpy(&old_size, raw_ptr, sizeof(old_size));

    new_raw = (uint8_t*)realloc(raw_ptr, header + new_size);
    if (!new_raw) {
        return NULL;
    }

    memcpy(new_raw, &new_size, sizeof(new_size));

    if (g_memory_stats.tracking_enabled) {
        memory_track_free(old_size);
        memory_track_alloc(new_size);
    }

    return new_raw + header;
}

static void memory_deallocate(void* ptr) {
    size_t header = memory_header_size();
    size_t size = 0;
    uint8_t* raw_ptr;

    if (!ptr) {
        return;
    }

    raw_ptr = ((uint8_t*)ptr) - header;
    memcpy(&size, raw_ptr, sizeof(size));

    if (g_memory_stats.tracking_enabled) {
        memory_track_free(size);
    }

    free(raw_ptr);
}

static size_t memory_get_allocated_size(void* ptr) {
    size_t header = memory_header_size();
    size_t size = 0;
    if (!ptr) {
        return 0;
    }
    memcpy(&size, ((uint8_t*)ptr) - header, sizeof(size));
    return size;
}

static void memory_dump_stats(void) {
    printf("=== HOSC Memory Statistics ===\n");
    printf("Total Allocated: %zu bytes\n", g_memory_stats.total_allocated);
    printf("Total Deallocated: %zu bytes\n", g_memory_stats.total_deallocated);
    printf("Current In Use: %zu bytes\n", g_memory_stats.current_in_use);
    printf("Peak Usage: %zu bytes\n", g_memory_stats.peak_usage);
    printf("Allocation Count: %zu\n", g_memory_stats.allocation_count);
    printf("==============================\n");
}

static HOSCMemoryManager g_memory_manager = {
    .allocate = memory_allocate,
    .reallocate = memory_reallocate,
    .deallocate = memory_deallocate,
    .get_allocated_size = memory_get_allocated_size,
    .dump_memory_stats = memory_dump_stats
};

// ============================================================================
// GUI BACKEND
// ============================================================================

#define HOSC_GUI_EVENT_QUEUE_CAP 256

static HOSCGUIBackend g_gui_backend = HOSC_GUI_BACKEND_CONSOLE;
static bool g_gui_initialized = false;
static bool g_gui_running = false;
static HOSCGUIEvent g_gui_event_queue[HOSC_GUI_EVENT_QUEUE_CAP];
static size_t g_gui_event_head = 0;
static size_t g_gui_event_tail = 0;

static void hosc_gui_clear_event_queue(void) {
    g_gui_event_head = 0;
    g_gui_event_tail = 0;
}

static bool hosc_gui_event_queue_is_full(void) {
    size_t next_tail = (g_gui_event_tail + 1U) % HOSC_GUI_EVENT_QUEUE_CAP;
    return next_tail == g_gui_event_head;
}

static void hosc_gui_push_event(HOSCGUIEventType type, int key_code, int x, int y, int button) {
    HOSCGUIEvent event;

    event.type = type;
    event.key_code = key_code;
    event.mouse_x = x;
    event.mouse_y = y;
    event.mouse_button = button;

    if (hosc_gui_event_queue_is_full()) {
        g_gui_event_head = (g_gui_event_head + 1U) % HOSC_GUI_EVENT_QUEUE_CAP;
    }

    g_gui_event_queue[g_gui_event_tail] = event;
    g_gui_event_tail = (g_gui_event_tail + 1U) % HOSC_GUI_EVENT_QUEUE_CAP;
}

static bool hosc_gui_pop_event(HOSCGUIEvent* out_event) {
    if (g_gui_event_head == g_gui_event_tail) {
        return false;
    }

    if (out_event) {
        *out_event = g_gui_event_queue[g_gui_event_head];
    }

    g_gui_event_head = (g_gui_event_head + 1U) % HOSC_GUI_EVENT_QUEUE_CAP;
    return true;
}

#ifdef _WIN32
static const char* HOSC_WINDOW_CLASS_NAME = "HOSCFrameworkWindowClass";
static HWND g_gui_window = NULL;
static bool g_gui_class_registered = false;

static int hosc_lparam_x(LPARAM value) {
    return (int)(short)LOWORD(value);
}

static int hosc_lparam_y(LPARAM value) {
    return (int)(short)HIWORD(value);
}

static LRESULT CALLBACK hosc_window_proc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam) {
    switch (msg) {
        case WM_CLOSE:
            hosc_gui_push_event(HOSC_GUI_EVENT_QUIT, 0, 0, 0, 0);
            DestroyWindow(hwnd);
            return 0;
        case WM_DESTROY:
            g_gui_running = false;
            hosc_gui_push_event(HOSC_GUI_EVENT_QUIT, 0, 0, 0, 0);
            return 0;
        case WM_KEYDOWN:
            hosc_gui_push_event(HOSC_GUI_EVENT_KEY_DOWN, (int)wparam, 0, 0, 0);
            return 0;
        case WM_KEYUP:
            hosc_gui_push_event(HOSC_GUI_EVENT_KEY_UP, (int)wparam, 0, 0, 0);
            return 0;
        case WM_MOUSEMOVE:
            hosc_gui_push_event(HOSC_GUI_EVENT_MOUSE_MOVE, 0, hosc_lparam_x(lparam), hosc_lparam_y(lparam), 0);
            return 0;
        case WM_LBUTTONDOWN:
            hosc_gui_push_event(HOSC_GUI_EVENT_MOUSE_DOWN, 0, hosc_lparam_x(lparam), hosc_lparam_y(lparam), 1);
            return 0;
        case WM_LBUTTONUP:
            hosc_gui_push_event(HOSC_GUI_EVENT_MOUSE_UP, 0, hosc_lparam_x(lparam), hosc_lparam_y(lparam), 1);
            return 0;
        case WM_RBUTTONDOWN:
            hosc_gui_push_event(HOSC_GUI_EVENT_MOUSE_DOWN, 0, hosc_lparam_x(lparam), hosc_lparam_y(lparam), 2);
            return 0;
        case WM_RBUTTONUP:
            hosc_gui_push_event(HOSC_GUI_EVENT_MOUSE_UP, 0, hosc_lparam_x(lparam), hosc_lparam_y(lparam), 2);
            return 0;
        case WM_MBUTTONDOWN:
            hosc_gui_push_event(HOSC_GUI_EVENT_MOUSE_DOWN, 0, hosc_lparam_x(lparam), hosc_lparam_y(lparam), 3);
            return 0;
        case WM_MBUTTONUP:
            hosc_gui_push_event(HOSC_GUI_EVENT_MOUSE_UP, 0, hosc_lparam_x(lparam), hosc_lparam_y(lparam), 3);
            return 0;
        default:
            return DefWindowProcA(hwnd, msg, wparam, lparam);
    }
}

static bool hosc_register_window_class(void) {
    WNDCLASSA wc;

    if (g_gui_class_registered) {
        return true;
    }

    memset(&wc, 0, sizeof(wc));
    wc.lpfnWndProc = hosc_window_proc;
    wc.hInstance = GetModuleHandleA(NULL);
    wc.lpszClassName = HOSC_WINDOW_CLASS_NAME;
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);
    wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);

    if (!RegisterClassA(&wc)) {
        return false;
    }

    g_gui_class_registered = true;
    return true;
}
#endif

bool hosc_gui_init(void) {
    if (g_gui_initialized) {
        return true;
    }

    g_gui_backend = HOSC_GUI_BACKEND_CONSOLE;
    hosc_gui_clear_event_queue();

#ifdef _WIN32
    if (hosc_register_window_class()) {
        g_gui_backend = HOSC_GUI_BACKEND_WIN32;
    }
#endif

    g_gui_initialized = true;
    g_gui_running = false;
    return true;
}

void hosc_gui_shutdown(void) {
#ifdef _WIN32
    if (g_gui_window) {
        DestroyWindow(g_gui_window);
        g_gui_window = NULL;
    }
#endif

    hosc_gui_clear_event_queue();
    g_gui_running = false;
    g_gui_backend = HOSC_GUI_BACKEND_CONSOLE;
    g_gui_initialized = false;
}

HOSCGUIBackend hosc_gui_backend(void) {
    return g_gui_backend;
}

const char* hosc_gui_backend_name(void) {
    switch (g_gui_backend) {
        case HOSC_GUI_BACKEND_WIN32:
            return "win32";
        case HOSC_GUI_BACKEND_CONSOLE:
        default:
            return "console";
    }
}

bool hosc_gui_create_window(const char* title, int width, int height) {
    if (!g_gui_initialized) {
        hosc_gui_init();
    }

    if (!title) {
        title = "HOSC Window";
    }

#ifdef _WIN32
    if (g_gui_backend == HOSC_GUI_BACKEND_WIN32) {
        if (g_gui_window) {
            DestroyWindow(g_gui_window);
            g_gui_window = NULL;
        }

        g_gui_window = CreateWindowExA(
            0,
            HOSC_WINDOW_CLASS_NAME,
            title,
            WS_OVERLAPPEDWINDOW,
            CW_USEDEFAULT,
            CW_USEDEFAULT,
            (width > 0 ? width : 800),
            (height > 0 ? height : 600),
            NULL,
            NULL,
            GetModuleHandleA(NULL),
            NULL
        );

        if (g_gui_window) {
            ShowWindow(g_gui_window, SW_SHOW);
            UpdateWindow(g_gui_window);
            g_gui_running = true;
            hosc_gui_clear_event_queue();
            return true;
        }

        g_gui_backend = HOSC_GUI_BACKEND_CONSOLE;
    }
#endif

    printf("[GUI:console] create_window title=\"%s\" size=%dx%d\n", title,
           (width > 0 ? width : 800), (height > 0 ? height : 600));
    g_gui_running = true;
    hosc_gui_clear_event_queue();
    return true;
}

void hosc_gui_draw_text(int x, int y, const char* text) {
    if (!text) {
        text = "";
    }

#ifdef _WIN32
    if (g_gui_backend == HOSC_GUI_BACKEND_WIN32 && g_gui_window) {
        HDC dc = GetDC(g_gui_window);
        if (dc) {
            TextOutA(dc, x, y, text, (int)strlen(text));
            ReleaseDC(g_gui_window, dc);
        }
        hosc_gui_pump_events();
        return;
    }
#endif

    printf("[GUI:console] text x=%d y=%d msg=\"%s\"\n", x, y, text);
}

void hosc_gui_pump_events(void) {
#ifdef _WIN32
    MSG msg;

    while (PeekMessageA(&msg, NULL, 0, 0, PM_REMOVE)) {
        TranslateMessage(&msg);
        DispatchMessageA(&msg);
    }
#endif
}

bool hosc_gui_poll_event(HOSCGUIEvent* out_event) {
    HOSCGUIEvent empty_event;

    hosc_gui_pump_events();

    if (hosc_gui_pop_event(out_event)) {
        return true;
    }

    if (out_event) {
        empty_event.type = HOSC_GUI_EVENT_NONE;
        empty_event.key_code = 0;
        empty_event.mouse_x = 0;
        empty_event.mouse_y = 0;
        empty_event.mouse_button = 0;
        *out_event = empty_event;
    }

    return false;
}

bool hosc_gui_is_running(void) {
    return g_gui_running;
}
// ============================================================================
// MODULE REGISTRY
// ============================================================================

typedef struct ModuleEntry {
    HOSCModule* module;
    struct ModuleEntry* next;
} ModuleEntry;

static ModuleEntry* g_module_list = NULL;

static bool module_registry_add(HOSCModule* module) {
    ModuleEntry* entry;

    if (!module) {
        return false;
    }

    entry = g_module_list;
    while (entry) {
        if (strcmp(entry->module->name, module->name) == 0) {
            return true;
        }
        entry = entry->next;
    }

    entry = (ModuleEntry*)memory_allocate(sizeof(ModuleEntry));
    if (!entry) {
        return false;
    }

    entry->module = module;
    entry->next = g_module_list;
    g_module_list = entry;

    if (module->init) {
        module->init();
    }

    return true;
}

static HOSCModule* module_get(const char* module_name) {
    ModuleEntry* entry = g_module_list;
    while (entry) {
        if (strcmp(entry->module->name, module_name) == 0) {
            return entry->module;
        }
        entry = entry->next;
    }
    return NULL;
}

static HOSCModule* module_load(const char* module_name) {
    HOSCModule* module = NULL;

    if (!module_name) {
        return NULL;
    }

    module = module_get(module_name);
    if (module) {
        return module;
    }

    if (strcmp(module_name, "core") == 0) {
        module = &hosc_core_module;
    } else if (strcmp(module_name, "io") == 0) {
        module = &hosc_io_module;
    } else if (strcmp(module_name, "math") == 0) {
        module = &hosc_math_module;
    } else if (strcmp(module_name, "string") == 0) {
        module = &hosc_string_module;
    } else if (strcmp(module_name, "win32") == 0) {
        module = &hosc_win32_module;
    } else if (strcmp(module_name, "gui") == 0) {
        module = &hosc_gui_module;
    }

    if (!module) {
        return NULL;
    }

    if (!module_registry_add(module)) {
        return NULL;
    }

    return module;
}

static void module_unload(HOSCModule* module) {
    ModuleEntry** current = &g_module_list;

    while (*current) {
        if ((*current)->module == module) {
            ModuleEntry* to_remove = *current;
            *current = (*current)->next;
            if (to_remove->module && to_remove->module->cleanup) {
                to_remove->module->cleanup(NULL);
            }
            memory_deallocate(to_remove);
            return;
        }
        current = &(*current)->next;
    }
}

static void module_list_all(void) {
    ModuleEntry* entry = g_module_list;
    printf("=== Loaded Modules ===\n");
    while (entry) {
        printf("- %s v%s\n", entry->module->name, entry->module->version);
        entry = entry->next;
    }
    printf("======================\n");
}

static HOSCModuleRegistry g_module_registry = {
    .load_module = module_load,
    .unload_module = module_unload,
    .get_module = module_get,
    .list_modules = module_list_all
};

// ============================================================================
// API REGISTRY
// ============================================================================

typedef struct APIFunctionEntry {
    HOSCAPIFunction* function;
    struct APIFunctionEntry* next;
} APIFunctionEntry;

static APIFunctionEntry* g_api_list = NULL;

static HOSCAPIFunction* api_register_function(const char* name, const char* signature, void* implementation) {
    HOSCAPIFunction* func;
    APIFunctionEntry* entry;

    func = (HOSCAPIFunction*)memory_allocate(sizeof(HOSCAPIFunction));
    if (!func) {
        return NULL;
    }

    func->name = hosc_strdup_local(name ? name : "");
    func->signature = hosc_strdup_local(signature ? signature : "");
    func->implementation = implementation;
    func->validate_args = NULL;

    entry = (APIFunctionEntry*)memory_allocate(sizeof(APIFunctionEntry));
    if (!entry) {
        memory_deallocate((void*)func->name);
        memory_deallocate((void*)func->signature);
        memory_deallocate(func);
        return NULL;
    }

    entry->function = func;
    entry->next = g_api_list;
    g_api_list = entry;

    return func;
}

static HOSCAPIFunction* api_get_function(const char* name) {
    APIFunctionEntry* entry = g_api_list;
    while (entry) {
        if (strcmp(entry->function->name, name) == 0) {
            return entry->function;
        }
        entry = entry->next;
    }
    return NULL;
}

static void api_unregister_function(const char* name) {
    APIFunctionEntry** entry = &g_api_list;
    while (*entry) {
        if (strcmp((*entry)->function->name, name) == 0) {
            APIFunctionEntry* to_remove = *entry;
            *entry = (*entry)->next;
            memory_deallocate((void*)to_remove->function->name);
            memory_deallocate((void*)to_remove->function->signature);
            memory_deallocate(to_remove->function);
            memory_deallocate(to_remove);
            return;
        }
        entry = &(*entry)->next;
    }
}

static void api_list_functions(void) {
    APIFunctionEntry* entry = g_api_list;
    printf("=== Registered API Functions ===\n");
    while (entry) {
        printf("- %s: %s\n", entry->function->name, entry->function->signature);
        entry = entry->next;
    }
    printf("===============================\n");
}

static HOSCAPIRegistry g_api_registry = {
    .register_function = api_register_function,
    .get_function = api_get_function,
    .unregister_function = api_unregister_function,
    .list_functions = api_list_functions
};

// ============================================================================
// ERROR HANDLING
// ============================================================================

static HOSCError* g_last_error = NULL;
static void (*g_error_handler)(HOSCError*) = NULL;

static void error_report(HOSCError* error) {
    if (g_last_error) {
        memory_deallocate(g_last_error);
    }

    g_last_error = error;

    if (g_error_handler) {
        g_error_handler(error);
    } else {
        printf("HOSC Error [%d]: %s\n", error->code, error->message);
        if (error->file) {
            printf("  File: %s:%d\n", error->file, error->line);
        }
    }
}

static void error_clear(void) {
    if (g_last_error) {
        memory_deallocate(g_last_error);
        g_last_error = NULL;
    }
}

static HOSCError* error_get_last(void) {
    return g_last_error;
}

static void error_set_handler(void (*handler)(HOSCError*)) {
    g_error_handler = handler;
}

static HOSCErrorHandler g_error_handler_impl = {
    .report_error = error_report,
    .clear_errors = error_clear,
    .get_last_error = error_get_last,
    .set_error_handler = error_set_handler
};

// ============================================================================
// LOGGER
// ============================================================================

static HOSCLogLevel g_log_level = HOSC_LOG_INFO;
static FILE* g_log_file = NULL;

static const char* log_level_to_string(HOSCLogLevel level) {
    switch (level) {
        case HOSC_LOG_DEBUG:
            return "DEBUG";
        case HOSC_LOG_INFO:
            return "INFO";
        case HOSC_LOG_WARNING:
            return "WARNING";
        case HOSC_LOG_ERROR:
            return "ERROR";
        case HOSC_LOG_FATAL:
            return "FATAL";
        default:
            return "UNKNOWN";
    }
}

static void logger_log(HOSCLogLevel level, const char* message, ...) {
    FILE* output;
    time_t now;
    struct tm* tm_info;
    char timestamp[64];

    if (level < g_log_level) {
        return;
    }

    now = time(NULL);
    tm_info = localtime(&now);
    if (tm_info) {
        strftime(timestamp, sizeof(timestamp), "%Y-%m-%d %H:%M:%S", tm_info);
    } else {
        strcpy(timestamp, "0000-00-00 00:00:00");
    }

    output = g_log_file ? g_log_file : stdout;
    fprintf(output, "[%s] [%s] %s\n", timestamp, log_level_to_string(level), message);

    if (g_log_file) {
        fflush(g_log_file);
    }
}

static void logger_set_level(HOSCLogLevel level) {
    g_log_level = level;
}

static void logger_set_output(const char* file) {
    if (g_log_file && g_log_file != stdout) {
        fclose(g_log_file);
    }

    if (file) {
        g_log_file = fopen(file, "a");
        if (!g_log_file) {
            g_log_file = stdout;
        }
    } else {
        g_log_file = stdout;
    }
}

static void logger_flush(void) {
    if (g_log_file) {
        fflush(g_log_file);
    }
}

static HOSCLogger g_logger_impl = {
    .log = logger_log,
    .set_level = logger_set_level,
    .set_output = logger_set_output,
    .flush = logger_flush
};

// ============================================================================
// STANDARD LIBRARY
// ============================================================================

HOSCString* hosc_string_create(const char* data) {
    HOSCString* str;

    str = (HOSCString*)memory_allocate(sizeof(HOSCString));
    if (!str) {
        return NULL;
    }

    if (!data) {
        data = "";
    }

    str->length = strlen(data);
    str->data = (char*)memory_allocate(str->length + 1);
    if (!str->data) {
        memory_deallocate(str);
        return NULL;
    }

    memcpy(str->data, data, str->length + 1);
    return str;
}

void hosc_string_destroy(HOSCString* str) {
    if (!str) {
        return;
    }

    if (str->data) {
        memory_deallocate(str->data);
    }

    memory_deallocate(str);
}

HOSCArray* hosc_array_create(size_t initial_capacity) {
    HOSCArray* array = (HOSCArray*)memory_allocate(sizeof(HOSCArray));
    if (!array) {
        return NULL;
    }

    array->capacity = (initial_capacity > 0 ? initial_capacity : 8);
    array->count = 0;
    array->items = (void**)memory_allocate(sizeof(void*) * array->capacity);
    if (!array->items) {
        memory_deallocate(array);
        return NULL;
    }

    return array;
}

void hosc_array_destroy(HOSCArray* array) {
    if (!array) {
        return;
    }

    if (array->items) {
        memory_deallocate(array->items);
    }

    memory_deallocate(array);
}

HOSCDictionary* hosc_dictionary_create(size_t initial_capacity) {
    HOSCDictionary* dict = (HOSCDictionary*)memory_allocate(sizeof(HOSCDictionary));
    if (!dict) {
        return NULL;
    }

    dict->capacity = (initial_capacity > 0 ? initial_capacity : 8);
    dict->count = 0;
    dict->keys = (void**)memory_allocate(sizeof(void*) * dict->capacity);
    dict->values = (void**)memory_allocate(sizeof(void*) * dict->capacity);

    if (!dict->keys || !dict->values) {
        if (dict->keys) {
            memory_deallocate(dict->keys);
        }
        if (dict->values) {
            memory_deallocate(dict->values);
        }
        memory_deallocate(dict);
        return NULL;
    }

    return dict;
}

void hosc_dictionary_destroy(HOSCDictionary* dict) {
    if (!dict) {
        return;
    }

    if (dict->keys) {
        memory_deallocate(dict->keys);
    }
    if (dict->values) {
        memory_deallocate(dict->values);
    }

    memory_deallocate(dict);
}

// ============================================================================
// RUNTIME CORE
// ============================================================================

HOSCRuntimeContext* hosc_runtime_init(const HOSCRuntimeConfig* config) {
    if (g_runtime_context) {
        return g_runtime_context;
    }

    g_runtime_context = (HOSCRuntimeContext*)memory_allocate(sizeof(HOSCRuntimeContext));
    if (!g_runtime_context) {
        return NULL;
    }

    memset(g_runtime_context, 0, sizeof(HOSCRuntimeContext));
    g_runtime_context->state = HOSC_RUNTIME_STATE_INITIALIZED;
    g_runtime_context->runtime_id = ++g_runtime_counter;
    g_runtime_context->start_time = hosc_now_ms();

    if (config) {
        g_runtime_context->config = *config;
    } else {
        g_runtime_context->config.enable_debug = true;
        g_runtime_context->config.enable_memory_tracking = true;
        g_runtime_context->config.enable_garbage_collection = false;
        g_runtime_context->config.max_memory_mb = 100;
        g_runtime_context->config.log_file = NULL;
    }

    g_runtime_context->memory_manager = &g_memory_manager;
    g_runtime_context->module_registry = &g_module_registry;
    g_runtime_context->api_registry = &g_api_registry;
    g_runtime_context->error_handler = &g_error_handler_impl;
    g_runtime_context->logger = &g_logger_impl;

    g_memory_stats.tracking_enabled = g_runtime_context->config.enable_memory_tracking;

    logger_set_output(g_runtime_context->config.log_file);
    hosc_gui_init();

    g_runtime_context->state = HOSC_RUNTIME_STATE_RUNNING;

    hosc_log(HOSC_LOG_INFO, "HOSC Runtime initialized (v%s), GUI backend=%s",
             HOSC_RUNTIME_VERSION, hosc_gui_backend_name());

    return g_runtime_context;
}

void hosc_runtime_shutdown(HOSCRuntimeContext* context) {
    ModuleEntry* module_entry;
    APIFunctionEntry* api_entry;

    if (!context || context != g_runtime_context) {
        return;
    }

    hosc_log(HOSC_LOG_INFO, "Shutting down HOSC Runtime");
    context->state = HOSC_RUNTIME_STATE_SHUTDOWN;

    while (g_module_list) {
        module_entry = g_module_list;
        g_module_list = g_module_list->next;
        if (module_entry->module && module_entry->module->cleanup) {
            module_entry->module->cleanup(NULL);
        }
        memory_deallocate(module_entry);
    }

    while (g_api_list) {
        api_entry = g_api_list;
        g_api_list = g_api_list->next;
        memory_deallocate((void*)api_entry->function->name);
        memory_deallocate((void*)api_entry->function->signature);
        memory_deallocate(api_entry->function);
        memory_deallocate(api_entry);
    }

    if (g_last_error) {
        memory_deallocate(g_last_error);
        g_last_error = NULL;
    }

    hosc_gui_shutdown();

    if (g_log_file && g_log_file != stdout) {
        fclose(g_log_file);
    }
    g_log_file = NULL;

    memory_deallocate(g_runtime_context);
    g_runtime_context = NULL;

    if (g_memory_stats.tracking_enabled) {
        memory_dump_stats();
    }
}

HOSCRuntimeState hosc_runtime_get_state(HOSCRuntimeContext* context) {
    if (!context) {
        return HOSC_RUNTIME_STATE_ERROR;
    }
    return context->state;
}

HOSCMemoryManager* hosc_runtime_get_memory_manager(HOSCRuntimeContext* context) {
    return context ? (HOSCMemoryManager*)context->memory_manager : NULL;
}

void* hosc_allocate(size_t size) {
    return memory_allocate(size);
}

void* hosc_reallocate(void* ptr, size_t new_size) {
    return memory_reallocate(ptr, new_size);
}

void hosc_deallocate(void* ptr) {
    memory_deallocate(ptr);
}

HOSCModuleRegistry* hosc_runtime_get_module_registry(HOSCRuntimeContext* context) {
    return context ? (HOSCModuleRegistry*)context->module_registry : NULL;
}

HOSCModule* hosc_load_module(const char* module_name) {
    return module_load(module_name);
}

void hosc_unload_module(HOSCModule* module) {
    module_unload(module);
}

HOSCAPIRegistry* hosc_runtime_get_api_registry(HOSCRuntimeContext* context) {
    return context ? (HOSCAPIRegistry*)context->api_registry : NULL;
}

void* hosc_call_function(const char* function_name, void* args) {
    HOSCAPIFunction* func = api_get_function(function_name);
    if (func && func->implementation) {
        return func->implementation(args);
    }
    return NULL;
}

HOSCErrorHandler* hosc_runtime_get_error_handler(HOSCRuntimeContext* context) {
    return context ? (HOSCErrorHandler*)context->error_handler : NULL;
}

void hosc_report_error(HOSCErrorType type, int code, const char* message, const char* file, int line) {
    HOSCError* error = (HOSCError*)memory_allocate(sizeof(HOSCError));
    if (!error) {
        return;
    }

    error->type = type;
    error->code = code;
    error->message = message ? message : "unknown error";
    error->file = file;
    error->line = line;
    error->timestamp = hosc_now_ms();

    error_report(error);
}

HOSCLogger* hosc_runtime_get_logger(HOSCRuntimeContext* context) {
    return context ? (HOSCLogger*)context->logger : NULL;
}

void hosc_log(HOSCLogLevel level, const char* message, ...) {
    va_list args;
    char buffer[1024];

    va_start(args, message);
    vsnprintf(buffer, sizeof(buffer), message, args);
    va_end(args);

    logger_log(level, buffer);
}



