/* hosc_runtime.c - Legacy framework runtime implementation (deprecated) */

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

typedef struct {
    MemoryStats stats;
} HOSCMemoryState;

static HOSCMemoryState* hosc_memory_state(HOSCRuntimeContext* context) {
    return context ? (HOSCMemoryState*)context->memory_state : NULL;
}

static size_t memory_header_size(void) {
    return sizeof(size_t);
}

static void memory_track_alloc(HOSCRuntimeContext* context, size_t sz) {
    HOSCMemoryState* state = hosc_memory_state(context);
    if (!state || !state->stats.tracking_enabled) {
        return;
    }
    state->stats.total_allocated += sz;
    state->stats.current_in_use += sz;
    if (state->stats.current_in_use > state->stats.peak_usage) {
        state->stats.peak_usage = state->stats.current_in_use;
    }
    state->stats.allocation_count++;
}

static void memory_track_free(HOSCRuntimeContext* context, size_t sz) {
    HOSCMemoryState* state = hosc_memory_state(context);
    if (!state || !state->stats.tracking_enabled) {
        return;
    }
    state->stats.total_deallocated += sz;
    if (state->stats.current_in_use >= sz) {
        state->stats.current_in_use -= sz;
    } else {
        state->stats.current_in_use = 0;
    }
}

static void* memory_allocate(HOSCRuntimeContext* context, size_t size) {
    size_t header = memory_header_size();
    uint8_t* raw = (uint8_t*)malloc(header + size);
    if (!raw) {
        return NULL;
    }
    memcpy(raw, &size, sizeof(size));

    memory_track_alloc(context, size);
    return raw + header;
}

static void* memory_reallocate(HOSCRuntimeContext* context, void* ptr, size_t new_size) {
    size_t header = memory_header_size();
    size_t old_size = 0;
    uint8_t* raw_ptr;
    uint8_t* new_raw;

    if (!ptr) {
        return memory_allocate(context, new_size);
    }

    raw_ptr = ((uint8_t*)ptr) - header;
    memcpy(&old_size, raw_ptr, sizeof(old_size));

    new_raw = (uint8_t*)realloc(raw_ptr, header + new_size);
    if (!new_raw) {
        return NULL;
    }

    memcpy(new_raw, &new_size, sizeof(new_size));

    if (old_size != new_size) {
        memory_track_free(context, old_size);
        memory_track_alloc(context, new_size);
    }

    return new_raw + header;
}

static void memory_deallocate(HOSCRuntimeContext* context, void* ptr) {
    size_t header = memory_header_size();
    size_t size = 0;
    uint8_t* raw_ptr;

    if (!ptr) {
        return;
    }

    raw_ptr = ((uint8_t*)ptr) - header;
    memcpy(&size, raw_ptr, sizeof(size));

    memory_track_free(context, size);

    free(raw_ptr);
}

static size_t memory_get_allocated_size(HOSCRuntimeContext* context, void* ptr) {
    size_t header = memory_header_size();
    size_t size = 0;
    (void)context;
    if (!ptr) {
        return 0;
    }
    memcpy(&size, ((uint8_t*)ptr) - header, sizeof(size));
    return size;
}

static void memory_dump_stats(HOSCRuntimeContext* context) {
    HOSCMemoryState* state = hosc_memory_state(context);
    if (!state) {
        return;
    }
    printf("=== HOSC Memory Statistics ===\n");
    printf("Total Allocated: %zu bytes\n", state->stats.total_allocated);
    printf("Total Deallocated: %zu bytes\n", state->stats.total_deallocated);
    printf("Current In Use: %zu bytes\n", state->stats.current_in_use);
    printf("Peak Usage: %zu bytes\n", state->stats.peak_usage);
    printf("Allocation Count: %zu\n", state->stats.allocation_count);
    printf("==============================\n");
}

static char* hosc_strdup_local(HOSCRuntimeContext* context, const char* input) {
    size_t len;
    char* out;

    if (!input) {
        return NULL;
    }

    len = strlen(input);
    out = (char*)memory_allocate(context, len + 1);
    if (!out) {
        return NULL;
    }

    memcpy(out, input, len + 1);
    return out;
}

// ============================================================================
// MODULE REGISTRY
// ============================================================================

typedef struct ModuleEntry {
    HOSCModule* module;
    void* module_context;
    struct ModuleEntry* next;
} ModuleEntry;

typedef struct {
    ModuleEntry* list;
} HOSCModuleState;

static HOSCModuleState* hosc_module_state(HOSCRuntimeContext* context) {
    return context ? (HOSCModuleState*)context->module_state : NULL;
}

static bool module_registry_add(HOSCRuntimeContext* context, HOSCModule* module) {
    ModuleEntry* entry;
    HOSCModuleState* state = hosc_module_state(context);

    if (!context || !module || !state) {
        return false;
    }

    entry = state->list;
    while (entry) {
        if (strcmp(entry->module->name, module->name) == 0) {
            return true;
        }
        entry = entry->next;
    }

    entry = (ModuleEntry*)memory_allocate(context, sizeof(ModuleEntry));
    if (!entry) {
        return false;
    }

    entry->module = module;
    entry->module_context = NULL;
    entry->next = state->list;
    state->list = entry;

    if (module->init) {
        entry->module_context = module->init(context);
    }

    return true;
}

static HOSCModule* module_get(HOSCRuntimeContext* context, const char* module_name) {
    ModuleEntry* entry;
    HOSCModuleState* state = hosc_module_state(context);

    if (!state || !module_name) {
        return NULL;
    }

    entry = state->list;
    while (entry) {
        if (strcmp(entry->module->name, module_name) == 0) {
            return entry->module;
        }
        entry = entry->next;
    }
    return NULL;
}

static HOSCModule* module_load(HOSCRuntimeContext* context, const char* module_name) {
    HOSCModule* module = NULL;

    if (!context || !module_name) {
        return NULL;
    }

    module = module_get(context, module_name);
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

    if (!module_registry_add(context, module)) {
        return NULL;
    }

    return module;
}

static void module_unload(HOSCRuntimeContext* context, HOSCModule* module) {
    ModuleEntry** current;
    HOSCModuleState* state = hosc_module_state(context);

    if (!context || !state) {
        return;
    }

    current = &state->list;
    while (*current) {
        if ((*current)->module == module) {
            ModuleEntry* to_remove = *current;
            *current = (*current)->next;
            if (to_remove->module && to_remove->module->cleanup) {
                to_remove->module->cleanup(context, to_remove->module_context);
            }
            memory_deallocate(context, to_remove);
            return;
        }
        current = &(*current)->next;
    }
}

static void module_list_all(HOSCRuntimeContext* context) {
    ModuleEntry* entry;
    HOSCModuleState* state = hosc_module_state(context);

    if (!state) {
        return;
    }

    entry = state->list;
    printf("=== Loaded Modules ===\n");
    while (entry) {
        printf("- %s v%s\n", entry->module->name, entry->module->version);
        entry = entry->next;
    }
    printf("======================\n");
}

// ============================================================================
// API REGISTRY
// ============================================================================

typedef struct APIFunctionEntry {
    HOSCAPIFunction* function;
    struct APIFunctionEntry* next;
} APIFunctionEntry;

typedef struct {
    APIFunctionEntry* list;
} HOSCAPIState;

static HOSCAPIState* hosc_api_state(HOSCRuntimeContext* context) {
    return context ? (HOSCAPIState*)context->api_state : NULL;
}

static HOSCAPIFunction* api_register_function(HOSCRuntimeContext* context, const char* name, const char* signature, void* implementation) {
    HOSCAPIFunction* func;
    APIFunctionEntry* entry;
    HOSCAPIState* state = hosc_api_state(context);

    if (!context || !state) {
        return NULL;
    }

    func = (HOSCAPIFunction*)memory_allocate(context, sizeof(HOSCAPIFunction));
    if (!func) {
        return NULL;
    }

    func->name = hosc_strdup_local(context, name ? name : "");
    func->signature = hosc_strdup_local(context, signature ? signature : "");
    func->implementation = implementation;
    func->validate_args = NULL;

    entry = (APIFunctionEntry*)memory_allocate(context, sizeof(APIFunctionEntry));
    if (!entry) {
        memory_deallocate(context, (void*)func->name);
        memory_deallocate(context, (void*)func->signature);
        memory_deallocate(context, func);
        return NULL;
    }

    entry->function = func;
    entry->next = state->list;
    state->list = entry;

    return func;
}

static HOSCAPIFunction* api_get_function(HOSCRuntimeContext* context, const char* name) {
    APIFunctionEntry* entry;
    HOSCAPIState* state = hosc_api_state(context);

    if (!state || !name) {
        return NULL;
    }

    entry = state->list;
    while (entry) {
        if (strcmp(entry->function->name, name) == 0) {
            return entry->function;
        }
        entry = entry->next;
    }
    return NULL;
}

static void api_unregister_function(HOSCRuntimeContext* context, const char* name) {
    APIFunctionEntry** entry;
    HOSCAPIState* state = hosc_api_state(context);

    if (!state || !name) {
        return;
    }

    entry = &state->list;
    while (*entry) {
        if (strcmp((*entry)->function->name, name) == 0) {
            APIFunctionEntry* to_remove = *entry;
            *entry = (*entry)->next;
            memory_deallocate(context, (void*)to_remove->function->name);
            memory_deallocate(context, (void*)to_remove->function->signature);
            memory_deallocate(context, to_remove->function);
            memory_deallocate(context, to_remove);
            return;
        }
        entry = &(*entry)->next;
    }
}

static void api_list_functions(HOSCRuntimeContext* context) {
    APIFunctionEntry* entry;
    HOSCAPIState* state = hosc_api_state(context);

    if (!state) {
        return;
    }

    entry = state->list;
    printf("=== Registered API Functions ===\n");
    while (entry) {
        printf("- %s (%s)\n", entry->function->name, entry->function->signature);
        entry = entry->next;
    }
    printf("================================\n");
}

// ============================================================================
// ERROR HANDLING
// ============================================================================

typedef struct {
    HOSCError* last_error;
    void (*handler)(HOSCError*);
} HOSCErrorState;

static HOSCErrorState* hosc_error_state(HOSCRuntimeContext* context) {
    return context ? (HOSCErrorState*)context->error_state : NULL;
}

static void error_report(HOSCRuntimeContext* context, HOSCError* error) {
    HOSCErrorState* state = hosc_error_state(context);

    if (!state) {
        return;
    }

    if (state->last_error) {
        memory_deallocate(context, state->last_error);
    }

    state->last_error = error;

    if (state->handler) {
        state->handler(error);
    } else {
        printf("HOSC Error [%d]: %s\n", error->code, error->message);
        if (error->file) {
            printf("  File: %s:%d\n", error->file, error->line);
        }
    }
}

static void error_clear(HOSCRuntimeContext* context) {
    HOSCErrorState* state = hosc_error_state(context);

    if (!state) {
        return;
    }

    if (state->last_error) {
        memory_deallocate(context, state->last_error);
        state->last_error = NULL;
    }
}

static HOSCError* error_get_last(HOSCRuntimeContext* context) {
    HOSCErrorState* state = hosc_error_state(context);
    if (!state) {
        return NULL;
    }
    return state->last_error;
}

static void error_set_handler(HOSCRuntimeContext* context, void (*handler)(HOSCError*)) {
    HOSCErrorState* state = hosc_error_state(context);
    if (!state) {
        return;
    }
    state->handler = handler;
}

// ============================================================================
// LOGGER
// ============================================================================

typedef struct {
    HOSCLogLevel level;
    FILE* file;
} HOSCLoggerState;

static HOSCLoggerState* hosc_logger_state(HOSCRuntimeContext* context) {
    return context ? (HOSCLoggerState*)context->logger_state : NULL;
}

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

static void logger_log(HOSCRuntimeContext* context, HOSCLogLevel level, const char* message, ...) {
    FILE* output;
    time_t now;
    struct tm* tm_info;
    char timestamp[64];
    va_list args;
    char buffer[1024];
    HOSCLoggerState* state = hosc_logger_state(context);

    if (!state) {
        return;
    }

    if (level < state->level) {
        return;
    }

    now = time(NULL);
    tm_info = localtime(&now);
    if (tm_info) {
        strftime(timestamp, sizeof(timestamp), "%Y-%m-%d %H:%M:%S", tm_info);
    } else {
        strcpy(timestamp, "0000-00-00 00:00:00");
    }

    va_start(args, message);
    vsnprintf(buffer, sizeof(buffer), message, args);
    va_end(args);

    output = state->file ? state->file : stdout;
    fprintf(output, "[%s] [%s] %s\n", timestamp, log_level_to_string(level), buffer);

    if (state->file && state->file != stdout) {
        fflush(state->file);
    }
}

static void logger_set_level(HOSCRuntimeContext* context, HOSCLogLevel level) {
    HOSCLoggerState* state = hosc_logger_state(context);
    if (!state) {
        return;
    }
    state->level = level;
}

static void logger_set_output(HOSCRuntimeContext* context, const char* file) {
    HOSCLoggerState* state = hosc_logger_state(context);
    if (!state) {
        return;
    }

    if (state->file && state->file != stdout) {
        fclose(state->file);
    }

    if (file) {
        state->file = fopen(file, "a");
        if (!state->file) {
            state->file = stdout;
        }
    } else {
        state->file = stdout;
    }
}

static void logger_flush(HOSCRuntimeContext* context) {
    HOSCLoggerState* state = hosc_logger_state(context);
    if (!state) {
        return;
    }
    if (state->file) {
        fflush(state->file);
    }
}

// ============================================================================
// STANDARD LIBRARY
// ============================================================================

HOSCString* hosc_string_create(HOSCRuntimeContext* context, const char* data) {
    HOSCString* str;

    str = (HOSCString*)memory_allocate(context, sizeof(HOSCString));
    if (!str) {
        return NULL;
    }

    if (!data) {
        data = "";
    }

    str->length = strlen(data);
    str->data = (char*)memory_allocate(context, str->length + 1);
    if (!str->data) {
        memory_deallocate(context, str);
        return NULL;
    }

    memcpy(str->data, data, str->length + 1);
    return str;
}

void hosc_string_destroy(HOSCRuntimeContext* context, HOSCString* str) {
    if (!str) {
        return;
    }

    memory_deallocate(context, str->data);
    memory_deallocate(context, str);
}

HOSCArray* hosc_array_create(HOSCRuntimeContext* context, size_t initial_capacity) {
    HOSCArray* array = (HOSCArray*)memory_allocate(context, sizeof(HOSCArray));
    if (!array) {
        return NULL;
    }

    array->count = 0;
    array->capacity = initial_capacity > 0 ? initial_capacity : 4;
    array->items = (void**)memory_allocate(context, array->capacity * sizeof(void*));
    if (!array->items) {
        memory_deallocate(context, array);
        return NULL;
    }
    return array;
}

void hosc_array_destroy(HOSCRuntimeContext* context, HOSCArray* array) {
    if (!array) {
        return;
    }

    memory_deallocate(context, array->items);
    memory_deallocate(context, array);
}

HOSCDictionary* hosc_dictionary_create(HOSCRuntimeContext* context, size_t initial_capacity) {
    HOSCDictionary* dict = (HOSCDictionary*)memory_allocate(context, sizeof(HOSCDictionary));
    if (!dict) {
        return NULL;
    }

    dict->count = 0;
    dict->capacity = initial_capacity > 0 ? initial_capacity : 8;
    dict->keys = (void**)memory_allocate(context, dict->capacity * sizeof(void*));
    dict->values = (void**)memory_allocate(context, dict->capacity * sizeof(void*));
    if (!dict->keys || !dict->values) {
        if (dict->keys) {
            memory_deallocate(context, dict->keys);
        }
        if (dict->values) {
            memory_deallocate(context, dict->values);
        }
        memory_deallocate(context, dict);
        return NULL;
    }

    return dict;
}

void hosc_dictionary_destroy(HOSCRuntimeContext* context, HOSCDictionary* dict) {
    if (!dict) {
        return;
    }

    memory_deallocate(context, dict->keys);
    memory_deallocate(context, dict->values);
    memory_deallocate(context, dict);
}

// ============================================================================
// RUNTIME CORE
// ============================================================================

HOSCRuntimeContext* hosc_runtime_init(const HOSCRuntimeConfig* config) {
    HOSCRuntimeContext* context = (HOSCRuntimeContext*)calloc(1, sizeof(HOSCRuntimeContext));
    HOSCMemoryState* memory_state;
    HOSCModuleState* module_state;
    HOSCAPIState* api_state;
    HOSCErrorState* error_state;
    HOSCLoggerState* logger_state;

    if (!context) {
        return NULL;
    }

    context->state = HOSC_RUNTIME_STATE_INITIALIZED;
    context->runtime_id = (uint64_t)(uintptr_t)context;
    context->start_time = hosc_now_ms();

    if (config) {
        context->config = *config;
    } else {
        context->config.enable_debug = true;
        context->config.enable_memory_tracking = true;
        context->config.enable_garbage_collection = false;
        context->config.max_memory_mb = 100;
        context->config.log_file = NULL;
    }

    memory_state = (HOSCMemoryState*)calloc(1, sizeof(HOSCMemoryState));
    module_state = (HOSCModuleState*)calloc(1, sizeof(HOSCModuleState));
    api_state = (HOSCAPIState*)calloc(1, sizeof(HOSCAPIState));
    error_state = (HOSCErrorState*)calloc(1, sizeof(HOSCErrorState));
    logger_state = (HOSCLoggerState*)calloc(1, sizeof(HOSCLoggerState));

    if (!memory_state || !module_state || !api_state || !error_state || !logger_state) {
        free(memory_state);
        free(module_state);
        free(api_state);
        free(error_state);
        free(logger_state);
        free(context);
        return NULL;
    }

    memory_state->stats.tracking_enabled = context->config.enable_memory_tracking;
    logger_state->level = HOSC_LOG_INFO;
    logger_state->file = stdout;

    context->memory_state = memory_state;
    context->module_state = module_state;
    context->api_state = api_state;
    context->error_state = error_state;
    context->logger_state = logger_state;

    context->memory_manager = (HOSCMemoryManager*)calloc(1, sizeof(HOSCMemoryManager));
    context->module_registry = (HOSCModuleRegistry*)calloc(1, sizeof(HOSCModuleRegistry));
    context->api_registry = (HOSCAPIRegistry*)calloc(1, sizeof(HOSCAPIRegistry));
    context->error_handler = (HOSCErrorHandler*)calloc(1, sizeof(HOSCErrorHandler));
    context->logger = (HOSCLogger*)calloc(1, sizeof(HOSCLogger));

    if (!context->memory_manager || !context->module_registry || !context->api_registry ||
        !context->error_handler || !context->logger) {
        free(context->memory_manager);
        free(context->module_registry);
        free(context->api_registry);
        free(context->error_handler);
        free(context->logger);
        free(memory_state);
        free(module_state);
        free(api_state);
        free(error_state);
        free(logger_state);
        free(context);
        return NULL;
    }

    context->memory_manager->allocate = memory_allocate;
    context->memory_manager->reallocate = memory_reallocate;
    context->memory_manager->deallocate = memory_deallocate;
    context->memory_manager->get_allocated_size = memory_get_allocated_size;
    context->memory_manager->dump_memory_stats = memory_dump_stats;

    context->module_registry->load_module = module_load;
    context->module_registry->unload_module = module_unload;
    context->module_registry->get_module = module_get;
    context->module_registry->list_modules = module_list_all;

    context->api_registry->register_function = api_register_function;
    context->api_registry->get_function = api_get_function;
    context->api_registry->unregister_function = api_unregister_function;
    context->api_registry->list_functions = api_list_functions;

    context->error_handler->report_error = error_report;
    context->error_handler->clear_errors = error_clear;
    context->error_handler->get_last_error = error_get_last;
    context->error_handler->set_error_handler = error_set_handler;

    context->logger->log = logger_log;
    context->logger->set_level = logger_set_level;
    context->logger->set_output = logger_set_output;
    context->logger->flush = logger_flush;

    logger_set_output(context, context->config.log_file);
    hosc_gui_init(context);

    context->state = HOSC_RUNTIME_STATE_RUNNING;

    hosc_log(context, HOSC_LOG_INFO, "HOSC Runtime initialized (v%s), GUI backend=%s",
             HOSC_RUNTIME_VERSION, hosc_gui_backend_name(context));

    return context;
}

void hosc_runtime_shutdown(HOSCRuntimeContext* context) {
    HOSCModuleState* module_state;
    HOSCAPIState* api_state;
    HOSCErrorState* error_state;
    HOSCLoggerState* logger_state;

    if (!context) {
        return;
    }

    hosc_log(context, HOSC_LOG_INFO, "Shutting down HOSC Runtime");
    context->state = HOSC_RUNTIME_STATE_SHUTDOWN;

    module_state = hosc_module_state(context);
    api_state = hosc_api_state(context);
    error_state = hosc_error_state(context);
    logger_state = hosc_logger_state(context);

    while (module_state && module_state->list) {
        ModuleEntry* module_entry = module_state->list;
        module_state->list = module_state->list->next;
        if (module_entry->module && module_entry->module->cleanup) {
            module_entry->module->cleanup(context, module_entry->module_context);
        }
        memory_deallocate(context, module_entry);
    }

    while (api_state && api_state->list) {
        APIFunctionEntry* api_entry = api_state->list;
        api_state->list = api_state->list->next;
        memory_deallocate(context, (void*)api_entry->function->name);
        memory_deallocate(context, (void*)api_entry->function->signature);
        memory_deallocate(context, api_entry->function);
        memory_deallocate(context, api_entry);
    }

    if (error_state && error_state->last_error) {
        memory_deallocate(context, error_state->last_error);
        error_state->last_error = NULL;
    }

    hosc_gui_shutdown(context);

    if (logger_state && logger_state->file && logger_state->file != stdout) {
        fclose(logger_state->file);
    }
    if (logger_state) {
        logger_state->file = NULL;
    }

    if (context->memory_manager) {
        free(context->memory_manager);
    }
    if (context->module_registry) {
        free(context->module_registry);
    }
    if (context->api_registry) {
        free(context->api_registry);
    }
    if (context->error_handler) {
        free(context->error_handler);
    }
    if (context->logger) {
        free(context->logger);
    }

    if (context->config.enable_memory_tracking) {
        memory_dump_stats(context);
    }

    free(context->memory_state);
    free(context->module_state);
    free(context->api_state);
    free(context->error_state);
    free(context->logger_state);
    free(context);
}

HOSCRuntimeState hosc_runtime_get_state(HOSCRuntimeContext* context) {
    if (!context) {
        return HOSC_RUNTIME_STATE_ERROR;
    }
    return context->state;
}

HOSCMemoryManager* hosc_runtime_get_memory_manager(HOSCRuntimeContext* context) {
    return context ? context->memory_manager : NULL;
}

void* hosc_allocate(HOSCRuntimeContext* context, size_t size) {
    return memory_allocate(context, size);
}

void* hosc_reallocate(HOSCRuntimeContext* context, void* ptr, size_t new_size) {
    return memory_reallocate(context, ptr, new_size);
}

void hosc_deallocate(HOSCRuntimeContext* context, void* ptr) {
    memory_deallocate(context, ptr);
}

HOSCModuleRegistry* hosc_runtime_get_module_registry(HOSCRuntimeContext* context) {
    return context ? context->module_registry : NULL;
}

HOSCModule* hosc_load_module(HOSCRuntimeContext* context, const char* module_name) {
    return module_load(context, module_name);
}

void hosc_unload_module(HOSCRuntimeContext* context, HOSCModule* module) {
    module_unload(context, module);
}

HOSCAPIRegistry* hosc_runtime_get_api_registry(HOSCRuntimeContext* context) {
    return context ? context->api_registry : NULL;
}

void* hosc_call_function(HOSCRuntimeContext* context, const char* function_name, void* args) {
    HOSCAPIFunction* func = api_get_function(context, function_name);
    if (func && func->implementation) {
        return func->implementation(args);
    }
    return NULL;
}

HOSCErrorHandler* hosc_runtime_get_error_handler(HOSCRuntimeContext* context) {
    return context ? context->error_handler : NULL;
}

void hosc_report_error(HOSCRuntimeContext* context, HOSCErrorType type, int code, const char* message, const char* file, int line) {
    HOSCError* error = (HOSCError*)memory_allocate(context, sizeof(HOSCError));
    if (!error) {
        return;
    }

    error->type = type;
    error->code = code;
    error->message = message ? message : "unknown error";
    error->file = file;
    error->line = line;
    error->timestamp = hosc_now_ms();

    error_report(context, error);
}

HOSCLogger* hosc_runtime_get_logger(HOSCRuntimeContext* context) {
    return context ? context->logger : NULL;
}

void hosc_log(HOSCRuntimeContext* context, HOSCLogLevel level, const char* message, ...) {
    va_list args;
    char buffer[1024];

    va_start(args, message);
    vsnprintf(buffer, sizeof(buffer), message, args);
    va_end(args);

    logger_log(context, level, buffer);
}
