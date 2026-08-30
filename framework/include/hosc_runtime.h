/*
 * File: framework\include\hosc_runtime.h
 * Purpose: HOSC source file.
 */

#ifndef HOSC_RUNTIME_H
#define HOSC_RUNTIME_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

// ============================================================================
// HOSC RUNTIME FRAMEWORK - The "Soul" of HOSC Language
// ============================================================================

#define HOSC_RUNTIME_VERSION_MAJOR 1
#define HOSC_RUNTIME_VERSION_MINOR 0
#define HOSC_RUNTIME_VERSION_PATCH 0
#define HOSC_RUNTIME_VERSION "1.0.0"

// Runtime Configuration
typedef struct {
    bool enable_debug;
    bool enable_memory_tracking;
    bool enable_garbage_collection;
    size_t max_memory_mb;
    const char* log_file;
} HOSCRuntimeConfig;

// Runtime State
typedef enum {
    HOSC_RUNTIME_STATE_INITIALIZED,
    HOSC_RUNTIME_STATE_RUNNING,
    HOSC_RUNTIME_STATE_PAUSED,
    HOSC_RUNTIME_STATE_ERROR,
    HOSC_RUNTIME_STATE_SHUTDOWN
} HOSCRuntimeState;

// Runtime Context
typedef struct HOSCRuntimeContext {
    HOSCRuntimeState state;
    HOSCRuntimeConfig config;
    void* memory_manager;
    void* module_registry;
    void* api_registry;
    void* error_handler;
    void* logger;
    uint64_t runtime_id;
    uint64_t start_time;
} HOSCRuntimeContext;

// ============================================================================
// GUI FRAMEWORK
// ============================================================================

typedef enum {
    HOSC_GUI_BACKEND_CONSOLE = 0,
    HOSC_GUI_BACKEND_WIN32 = 1,
    HOSC_GUI_BACKEND_X11 = 2
} HOSCGUIBackend;

typedef enum {
    HOSC_GUI_EVENT_NONE = 0,
    HOSC_GUI_EVENT_QUIT,
    HOSC_GUI_EVENT_KEY_DOWN,
    HOSC_GUI_EVENT_KEY_UP,
    HOSC_GUI_EVENT_MOUSE_MOVE,
    HOSC_GUI_EVENT_MOUSE_DOWN,
    HOSC_GUI_EVENT_MOUSE_UP
} HOSCGUIEventType;

typedef struct {
    HOSCGUIEventType type;
    int key_code;
    int mouse_x;
    int mouse_y;
    int mouse_button;
} HOSCGUIEvent;

typedef struct {
    const char* title;
    int width;
    int height;
    bool resizable;
    bool fullscreen;
    const char* icon;
    int min_width;
    int min_height;
    bool center;
} HOSCGUIWindowOptions;

bool hosc_gui_init(void);
void hosc_gui_shutdown(void);
HOSCGUIBackend hosc_gui_backend(void);
const char* hosc_gui_backend_name(void);
void hosc_runtime_set_base_dir(const char* base_dir);
bool hosc_gui_create_window(const char* title, int width, int height);
bool hosc_gui_create_window_ex(const HOSCGUIWindowOptions* options);
void hosc_gui_draw_text(int x, int y, const char* text);
void hosc_gui_draw_text_styled(int x, int y, const char* text, int size, int r, int g, int b, bool bold);
void hosc_gui_draw_rect(int x, int y, int width, int height, int r, int g, int b, bool filled);
void hosc_gui_draw_round_rect(int x, int y, int width, int height, int radius,
                              int fill_r, int fill_g, int fill_b,
                              int border_r, int border_g, int border_b,
                              int border_width);
void hosc_gui_draw_image(int x, int y, int width, int height, const char* image_path);
void hosc_gui_suspend_present(void);
void hosc_gui_resume_present(void);
void hosc_gui_flush(void);
void hosc_gui_pump_events(void);
bool hosc_gui_poll_event(HOSCGUIEvent* out_event);
bool hosc_gui_is_running(void);
bool hosc_audio_play_file(const char* audio_path, bool async_play);
void hosc_audio_stop(void);
bool hosc_gui_pick_audio_file(char* out_path, size_t out_cap);
bool hosc_audio_has_internal_playback(void);
int hosc_audio_get_position_ms(void);
int hosc_audio_get_duration_ms(void);
bool hosc_audio_seek_ms(int position_ms);

// ============================================================================
// MEMORY MANAGEMENT FRAMEWORK
// ============================================================================

typedef struct {
    void* (*allocate)(size_t size);
    void* (*reallocate)(void* ptr, size_t new_size);
    void (*deallocate)(void* ptr);
    size_t (*get_allocated_size)(void* ptr);
    void (*dump_memory_stats)(void);
} HOSCMemoryManager;

// ============================================================================
// MODULE SYSTEM FRAMEWORK
// ============================================================================

typedef struct HOSCModule {
    const char* name;
    const char* version;
    void* (*init)(void);
    void (*cleanup)(void* context);
    void* (*get_function)(const char* function_name);
    bool (*is_loaded)(void);
} HOSCModule;

typedef struct {
    HOSCModule* (*load_module)(const char* module_name);
    void (*unload_module)(HOSCModule* module);
    HOSCModule* (*get_module)(const char* module_name);
    void (*list_modules)(void);
} HOSCModuleRegistry;

// ============================================================================
// API FRAMEWORK
// ============================================================================

typedef struct HOSCAPIFunction {
    const char* name;
    const char* signature;
    void* (*implementation)(void* args);
    bool (*validate_args)(void* args);
} HOSCAPIFunction;

typedef struct {
    HOSCAPIFunction* (*register_function)(const char* name, const char* signature, void* implementation);
    HOSCAPIFunction* (*get_function)(const char* name);
    void (*unregister_function)(const char* name);
    void (*list_functions)(void);
} HOSCAPIRegistry;

// ============================================================================
// ERROR HANDLING FRAMEWORK
// ============================================================================

typedef enum {
    HOSC_ERROR_NONE,
    HOSC_ERROR_SYNTAX,
    HOSC_ERROR_RUNTIME,
    HOSC_ERROR_MEMORY,
    HOSC_ERROR_MODULE,
    HOSC_ERROR_API,
    HOSC_ERROR_SYSTEM
} HOSCErrorType;

typedef struct {
    HOSCErrorType type;
    int code;
    const char* message;
    const char* file;
    int line;
    uint64_t timestamp;
} HOSCError;

typedef struct {
    void (*report_error)(HOSCError* error);
    void (*clear_errors)(void);
    HOSCError* (*get_last_error)(void);
    void (*set_error_handler)(void (*handler)(HOSCError*));
} HOSCErrorHandler;

// ============================================================================
// LOGGING FRAMEWORK
// ============================================================================

typedef enum {
    HOSC_LOG_DEBUG,
    HOSC_LOG_INFO,
    HOSC_LOG_WARNING,
    HOSC_LOG_ERROR,
    HOSC_LOG_FATAL
} HOSCLogLevel;

typedef struct {
    void (*log)(HOSCLogLevel level, const char* message, ...);
    void (*set_level)(HOSCLogLevel level);
    void (*set_output)(const char* file);
    void (*flush)(void);
} HOSCLogger;

// ============================================================================
// STANDARD LIBRARY FRAMEWORK
// ============================================================================

typedef struct {
    int32_t value;
} HOSCInt;

typedef struct {
    double value;
} HOSCFloat;

typedef struct {
    char* data;
    size_t length;
} HOSCString;

typedef struct {
    bool value;
} HOSCBool;

typedef struct {
    void** items;
    size_t count;
    size_t capacity;
} HOSCArray;

typedef struct {
    void** keys;
    void** values;
    size_t count;
    size_t capacity;
} HOSCDictionary;

// ============================================================================
// RUNTIME CORE FUNCTIONS
// ============================================================================

HOSCRuntimeContext* hosc_runtime_init(const HOSCRuntimeConfig* config);
void hosc_runtime_shutdown(HOSCRuntimeContext* context);
HOSCRuntimeState hosc_runtime_get_state(HOSCRuntimeContext* context);

HOSCMemoryManager* hosc_runtime_get_memory_manager(HOSCRuntimeContext* context);
void* hosc_allocate(size_t size);
void* hosc_reallocate(void* ptr, size_t new_size);
void hosc_deallocate(void* ptr);

HOSCModuleRegistry* hosc_runtime_get_module_registry(HOSCRuntimeContext* context);
HOSCModule* hosc_load_module(const char* module_name);
void hosc_unload_module(HOSCModule* module);

HOSCAPIRegistry* hosc_runtime_get_api_registry(HOSCRuntimeContext* context);
void* hosc_call_function(const char* function_name, void* args);

HOSCErrorHandler* hosc_runtime_get_error_handler(HOSCRuntimeContext* context);
void hosc_report_error(HOSCErrorType type, int code, const char* message, const char* file, int line);

HOSCLogger* hosc_runtime_get_logger(HOSCRuntimeContext* context);
void hosc_log(HOSCLogLevel level, const char* message, ...);

HOSCString* hosc_string_create(const char* data);
void hosc_string_destroy(HOSCString* str);
HOSCArray* hosc_array_create(size_t initial_capacity);
void hosc_array_destroy(HOSCArray* array);
HOSCDictionary* hosc_dictionary_create(size_t initial_capacity);
void hosc_dictionary_destroy(HOSCDictionary* dict);

// ============================================================================
// BUILT-IN MODULES
// ============================================================================

extern HOSCModule hosc_core_module;
extern HOSCModule hosc_io_module;
extern HOSCModule hosc_math_module;
extern HOSCModule hosc_string_module;
extern HOSCModule hosc_win32_module;
extern HOSCModule hosc_gui_module;

// ============================================================================
// UTILITY MACROS
// ============================================================================

#define HOSC_ERROR_REPORT(type, code, message) \
    hosc_report_error(type, code, message, __FILE__, __LINE__)

#define HOSC_LOG_DEBUG(message, ...) \
    hosc_log(HOSC_LOG_DEBUG, message, ##__VA_ARGS__)

#define HOSC_LOG_INFO(message, ...) \
    hosc_log(HOSC_LOG_INFO, message, ##__VA_ARGS__)

#define HOSC_LOG_WARNING(message, ...) \
    hosc_log(HOSC_LOG_WARNING, message, ##__VA_ARGS__)

#define HOSC_LOG_ERROR(message, ...) \
    hosc_log(HOSC_LOG_ERROR, message, ##__VA_ARGS__)

#define HOSC_LOG_FATAL(message, ...) \
    hosc_log(HOSC_LOG_FATAL, message, ##__VA_ARGS__)

#ifdef __cplusplus
}
#endif

#endif // HOSC_RUNTIME_H
