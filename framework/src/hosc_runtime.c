/*
 * File: framework\src\hosc_runtime.c
 * Purpose: HOSC source file.
 */

#ifndef WINVER
#define WINVER 0x0601
#endif
#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0601
#endif

#include "hosc_runtime.h"

#include <ctype.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#ifdef _WIN32
#include <windows.h>
#include <commdlg.h>
#include <gdiplus.h>
#include <mfapi.h>
#include <mfplay.h>
#include <propidl.h>
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
static char g_runtime_base_dir[1024] = {0};

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
static int g_gui_min_width = 0;
static int g_gui_min_height = 0;
static HICON g_gui_window_icon = NULL;
static ULONG_PTR g_gdiplus_token = 0;
static bool g_gdiplus_started = false;
static IMFPMediaPlayer* g_audio_player = NULL;
static bool g_audio_internal_playback = false;
static bool g_audio_ready = false;
static bool g_media_foundation_started = false;
static bool g_com_initialized = false;
static char g_last_audio_path[MAX_PATH] = {0};
static HDC g_gui_backbuffer_dc = NULL;
static HBITMAP g_gui_backbuffer_bitmap = NULL;
static HBITMAP g_gui_backbuffer_old_bitmap = NULL;
static int g_gui_backbuffer_width = 0;
static int g_gui_backbuffer_height = 0;
static int g_gui_present_suspend_count = 0;

static int hosc_utf8_to_wide(const char* input, WCHAR* output, int output_count);
static int hosc_wide_to_utf8(const WCHAR* input, char* output, int output_count);
static void hosc_normalize_windows_path(const char* input, char* output, size_t output_cap);

typedef struct {
    IMFPMediaPlayerCallback iface;
    LONG ref_count;
} HOSCMediaPlayerCallback;

static HRESULT STDMETHODCALLTYPE hosc_media_callback_query_interface(IMFPMediaPlayerCallback* self, REFIID riid, void** out_object);
static ULONG STDMETHODCALLTYPE hosc_media_callback_add_ref(IMFPMediaPlayerCallback* self);
static ULONG STDMETHODCALLTYPE hosc_media_callback_release(IMFPMediaPlayerCallback* self);
static void STDMETHODCALLTYPE hosc_media_callback_on_event(IMFPMediaPlayerCallback* self, MFP_EVENT_HEADER* event_header);

static const IMFPMediaPlayerCallbackVtbl g_hosc_media_callback_vtbl = {
    hosc_media_callback_query_interface,
    hosc_media_callback_add_ref,
    hosc_media_callback_release,
    hosc_media_callback_on_event
};

static HOSCMediaPlayerCallback g_hosc_media_callback = {
    { (IMFPMediaPlayerCallbackVtbl*)&g_hosc_media_callback_vtbl },
    1
};

static const IID HOSC_IID_IUNKNOWN = {0x00000000, 0x0000, 0x0000, {0xc0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x46}};
static const IID HOSC_IID_IMFPMEDIAPLAYERCALLBACK = {0x766c8ffb, 0x5fdb, 0x4fea, {0xa2, 0x8d, 0xb9, 0x12, 0x99, 0x6f, 0x51, 0xbd}};

static int hosc_lparam_x(LPARAM value) {
    return (int)(short)LOWORD(value);
}

static int hosc_lparam_y(LPARAM value) {
    return (int)(short)HIWORD(value);
}

static HRESULT STDMETHODCALLTYPE hosc_media_callback_query_interface(IMFPMediaPlayerCallback* self, REFIID riid, void** out_object) {
    if (!out_object) {
        return E_POINTER;
    }

    *out_object = NULL;
    if (InlineIsEqualGUID(riid, &HOSC_IID_IUNKNOWN) ||
        InlineIsEqualGUID(riid, &HOSC_IID_IMFPMEDIAPLAYERCALLBACK)) {
        *out_object = self;
        hosc_media_callback_add_ref(self);
        return S_OK;
    }

    return E_NOINTERFACE;
}

static ULONG STDMETHODCALLTYPE hosc_media_callback_add_ref(IMFPMediaPlayerCallback* self) {
    HOSCMediaPlayerCallback* callback = (HOSCMediaPlayerCallback*)self;
    return (ULONG)InterlockedIncrement(&callback->ref_count);
}

static ULONG STDMETHODCALLTYPE hosc_media_callback_release(IMFPMediaPlayerCallback* self) {
    HOSCMediaPlayerCallback* callback = (HOSCMediaPlayerCallback*)self;
    LONG value = InterlockedDecrement(&callback->ref_count);
    if (value < 1) {
        callback->ref_count = 1;
        value = 1;
    }
    return (ULONG)value;
}

static void STDMETHODCALLTYPE hosc_media_callback_on_event(IMFPMediaPlayerCallback* self, MFP_EVENT_HEADER* event_header) {
    (void)self;

    if (!event_header) {
        return;
    }

    if (FAILED(event_header->hrEvent)) {
        g_audio_ready = false;
        return;
    }

    switch (event_header->eEventType) {
        case MFP_EVENT_TYPE_MEDIAITEM_CREATED:
        case MFP_EVENT_TYPE_MEDIAITEM_SET:
        case MFP_EVENT_TYPE_PLAY:
        case MFP_EVENT_TYPE_POSITION_SET:
            g_audio_ready = true;
            break;
        case MFP_EVENT_TYPE_MEDIAITEM_CLEARED:
        case MFP_EVENT_TYPE_ERROR:
            g_audio_ready = false;
            break;
        case MFP_EVENT_TYPE_PLAYBACK_ENDED:
            g_audio_ready = true;
            break;
        default:
            break;
    }
}

static void hosc_destroy_window_icon(void) {
    if (g_gui_window_icon) {
        DestroyIcon(g_gui_window_icon);
        g_gui_window_icon = NULL;
    }
}

static void hosc_release_backbuffer(void) {
    if (g_gui_backbuffer_dc) {
        if (g_gui_backbuffer_old_bitmap) {
            SelectObject(g_gui_backbuffer_dc, g_gui_backbuffer_old_bitmap);
            g_gui_backbuffer_old_bitmap = NULL;
        }
        if (g_gui_backbuffer_bitmap) {
            DeleteObject(g_gui_backbuffer_bitmap);
            g_gui_backbuffer_bitmap = NULL;
        }
        DeleteDC(g_gui_backbuffer_dc);
        g_gui_backbuffer_dc = NULL;
    }
    g_gui_backbuffer_width = 0;
    g_gui_backbuffer_height = 0;
}

static bool hosc_ensure_backbuffer(int width, int height) {
    HDC window_dc;
    RECT rect;
    HBRUSH brush;

    if (!g_gui_window) {
        return false;
    }

    if (width <= 0 || height <= 0) {
        if (!GetClientRect(g_gui_window, &rect)) {
            return false;
        }
        width = rect.right - rect.left;
        height = rect.bottom - rect.top;
    }

    if (width <= 0 || height <= 0) {
        return false;
    }

    if (g_gui_backbuffer_dc &&
        g_gui_backbuffer_width == width &&
        g_gui_backbuffer_height == height) {
        return true;
    }

    hosc_release_backbuffer();

    window_dc = GetDC(g_gui_window);
    if (!window_dc) {
        return false;
    }

    g_gui_backbuffer_dc = CreateCompatibleDC(window_dc);
    if (!g_gui_backbuffer_dc) {
        ReleaseDC(g_gui_window, window_dc);
        return false;
    }

    g_gui_backbuffer_bitmap = CreateCompatibleBitmap(window_dc, width, height);
    ReleaseDC(g_gui_window, window_dc);
    if (!g_gui_backbuffer_bitmap) {
        hosc_release_backbuffer();
        return false;
    }

    g_gui_backbuffer_old_bitmap = (HBITMAP)SelectObject(g_gui_backbuffer_dc, g_gui_backbuffer_bitmap);
    g_gui_backbuffer_width = width;
    g_gui_backbuffer_height = height;

    brush = CreateSolidBrush(RGB(255, 255, 255));
    if (brush) {
        RECT fill_rect;
        fill_rect.left = 0;
        fill_rect.top = 0;
        fill_rect.right = width;
        fill_rect.bottom = height;
        FillRect(g_gui_backbuffer_dc, &fill_rect, brush);
        DeleteObject(brush);
    }

    return true;
}

static HDC hosc_gui_begin_draw(void) {
    if (g_gui_backend != HOSC_GUI_BACKEND_WIN32 || !g_gui_window) {
        return NULL;
    }

    if (!hosc_ensure_backbuffer(0, 0)) {
        return NULL;
    }

    return g_gui_backbuffer_dc;
}

static void hosc_gui_present(void) {
    HDC window_dc;

    if (!g_gui_window || !g_gui_backbuffer_dc || g_gui_backbuffer_width <= 0 || g_gui_backbuffer_height <= 0) {
        return;
    }
    if (g_gui_present_suspend_count > 0) {
        return;
    }

    window_dc = GetDC(g_gui_window);
    if (!window_dc) {
        return;
    }

    BitBlt(window_dc, 0, 0, g_gui_backbuffer_width, g_gui_backbuffer_height,
           g_gui_backbuffer_dc, 0, 0, SRCCOPY);
    ReleaseDC(g_gui_window, window_dc);
}

void hosc_gui_suspend_present(void) {
#ifdef _WIN32
    g_gui_present_suspend_count++;
#endif
}

void hosc_gui_resume_present(void) {
#ifdef _WIN32
    if (g_gui_present_suspend_count > 0) {
        g_gui_present_suspend_count--;
    }
#endif
}

void hosc_gui_flush(void) {
#ifdef _WIN32
    hosc_gui_present();
#endif
}

static void hosc_apply_window_icon(HWND hwnd, const char* icon_path) {
    HICON icon;
    char normalized_path[1024];
    WCHAR wide_path[1024];

    hosc_destroy_window_icon();
    if (!icon_path || !*icon_path) {
        return;
    }

    hosc_normalize_windows_path(icon_path, normalized_path, sizeof(normalized_path));
    if (!hosc_utf8_to_wide(normalized_path, wide_path, (int)(sizeof(wide_path) / sizeof(wide_path[0])))) {
        return;
    }

    icon = (HICON)LoadImageW(NULL, wide_path, IMAGE_ICON, 0, 0, LR_LOADFROMFILE | LR_DEFAULTSIZE);
    if (!icon) {
        return;
    }

    g_gui_window_icon = icon;
    SendMessageA(hwnd, WM_SETICON, ICON_SMALL, (LPARAM)icon);
    SendMessageA(hwnd, WM_SETICON, ICON_BIG, (LPARAM)icon);
}

static LRESULT CALLBACK hosc_window_proc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam) {
    switch (msg) {
        case WM_PAINT: {
            PAINTSTRUCT ps;
            HDC dc = BeginPaint(hwnd, &ps);
            if (dc) {
                if (hosc_ensure_backbuffer(0, 0) && g_gui_backbuffer_dc) {
                    BitBlt(dc, 0, 0, g_gui_backbuffer_width, g_gui_backbuffer_height,
                           g_gui_backbuffer_dc, 0, 0, SRCCOPY);
                }
                EndPaint(hwnd, &ps);
            }
            return 0;
        }
        case WM_ERASEBKGND:
            return 1;
        case WM_SIZE:
            hosc_ensure_backbuffer(LOWORD(lparam), HIWORD(lparam));
            return 0;
        case WM_CLOSE:
            hosc_gui_push_event(HOSC_GUI_EVENT_QUIT, 0, 0, 0, 0);
            DestroyWindow(hwnd);
            return 0;
        case WM_DESTROY:
            g_gui_running = false;
            hosc_release_backbuffer();
            g_gui_window = NULL;
            hosc_gui_push_event(HOSC_GUI_EVENT_QUIT, 0, 0, 0, 0);
            return 0;
        case WM_GETMINMAXINFO: {
            MINMAXINFO* minmax = (MINMAXINFO*)lparam;
            if (g_gui_min_width > 0) {
                minmax->ptMinTrackSize.x = g_gui_min_width;
            }
            if (g_gui_min_height > 0) {
                minmax->ptMinTrackSize.y = g_gui_min_height;
            }
            return 0;
        }
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

static bool hosc_gdiplus_init(void) {
    GdiplusStartupInput startup_input;
    GpStatus status;

    if (g_gdiplus_started) {
        return true;
    }

    memset(&startup_input, 0, sizeof(startup_input));
    startup_input.GdiplusVersion = 1;
    status = GdiplusStartup(&g_gdiplus_token, &startup_input, NULL);
    if (status != Ok) {
        g_gdiplus_token = 0;
        return false;
    }

    g_gdiplus_started = true;
    return true;
}

static void hosc_gdiplus_shutdown(void) {
    if (g_gdiplus_started) {
        GdiplusShutdown(g_gdiplus_token);
        g_gdiplus_token = 0;
        g_gdiplus_started = false;
    }
}

static int hosc_utf8_to_wide(const char* input, WCHAR* output, int output_count) {
    if (!input || !output || output_count <= 0) {
        return 0;
    }

    return MultiByteToWideChar(CP_UTF8, 0, input, -1, output, output_count);
}

static int hosc_wide_to_utf8(const WCHAR* input, char* output, int output_count) {
    if (!input || !output || output_count <= 0) {
        return 0;
    }

    return WideCharToMultiByte(CP_UTF8, 0, input, -1, output, output_count, NULL, NULL);
}

static int hosc_is_absolute_path(const char* input) {
    if (!input || !input[0]) {
        return 0;
    }

    if ((isalpha((unsigned char)input[0]) && input[1] == ':') ||
        (input[0] == '\\' && input[1] == '\\') ||
        input[0] == '/' ||
        input[0] == '\\') {
        return 1;
    }

    return 0;
}

void hosc_runtime_set_base_dir(const char* base_dir) {
    if (!base_dir || !base_dir[0]) {
        g_runtime_base_dir[0] = '\0';
        return;
    }

    strncpy(g_runtime_base_dir, base_dir, sizeof(g_runtime_base_dir) - 1);
    g_runtime_base_dir[sizeof(g_runtime_base_dir) - 1] = '\0';
}

static void hosc_normalize_windows_path(const char* input, char* output, size_t output_cap) {
    char candidate[2048];
    WCHAR wide_candidate[2048];
    WCHAR wide_full_path[2048];
    DWORD length;
    size_t i;

    if (!input || !output || output_cap == 0) {
        return;
    }

    output[0] = '\0';

    if (!hosc_is_absolute_path(input) && g_runtime_base_dir[0]) {
        snprintf(candidate, sizeof(candidate), "%s\\%s", g_runtime_base_dir, input);
    } else {
        strncpy(candidate, input, sizeof(candidate) - 1);
        candidate[sizeof(candidate) - 1] = '\0';
    }

    if (!hosc_utf8_to_wide(candidate, wide_candidate, (int)(sizeof(wide_candidate) / sizeof(wide_candidate[0])))) {
        strncpy(output, candidate, output_cap - 1);
        output[output_cap - 1] = '\0';
        return;
    }

    length = GetFullPathNameW(wide_candidate, (DWORD)(sizeof(wide_full_path) / sizeof(wide_full_path[0])), wide_full_path, NULL);
    if (length > 0 && length < (DWORD)(sizeof(wide_full_path) / sizeof(wide_full_path[0])) &&
        hosc_wide_to_utf8(wide_full_path, output, (int)output_cap)) {
        output[output_cap - 1] = '\0';
    } else {
        strncpy(output, candidate, output_cap - 1);
        output[output_cap - 1] = '\0';
    }

    for (i = 0; output[i] != '\0'; i++) {
        if (output[i] == '/') {
            output[i] = '\\';
        }
    }
}

static void hosc_audio_stop_internal(void) {
    if (g_audio_player) {
        g_audio_player->lpVtbl->Stop(g_audio_player);
        g_audio_player->lpVtbl->Shutdown(g_audio_player);
        g_audio_player->lpVtbl->Release(g_audio_player);
        g_audio_player = NULL;
    }
    g_audio_internal_playback = false;
    g_audio_ready = false;
}

static bool hosc_media_foundation_init(void) {
    HRESULT hr;

    if (g_media_foundation_started) {
        return true;
    }

    hr = CoInitializeEx(NULL, COINIT_APARTMENTTHREADED);
    if (SUCCEEDED(hr) || hr == RPC_E_CHANGED_MODE) {
        if (SUCCEEDED(hr)) {
            g_com_initialized = true;
        }
    } else {
        return false;
    }

    hr = MFStartup(MF_VERSION, MFSTARTUP_FULL);
    if (FAILED(hr)) {
        if (g_com_initialized) {
            CoUninitialize();
            g_com_initialized = false;
        }
        return false;
    }

    g_media_foundation_started = true;
    return true;
}

static void hosc_media_foundation_shutdown(void) {
    hosc_audio_stop_internal();

    if (g_media_foundation_started) {
        MFShutdown();
        g_media_foundation_started = false;
    }

    if (g_com_initialized) {
        CoUninitialize();
        g_com_initialized = false;
    }
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
    hosc_release_backbuffer();
    hosc_media_foundation_shutdown();
    hosc_gdiplus_shutdown();
    hosc_destroy_window_icon();
    g_gui_min_width = 0;
    g_gui_min_height = 0;
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
    HOSCGUIWindowOptions options;

    memset(&options, 0, sizeof(options));
    options.title = title;
    options.width = width;
    options.height = height;
    options.resizable = true;

    return hosc_gui_create_window_ex(&options);
}

bool hosc_gui_create_window_ex(const HOSCGUIWindowOptions* options) {
    const char* title = "HOSC Window";
    const char* icon = NULL;
    int width = 800;
    int height = 600;
    int min_width = 0;
    int min_height = 0;
    bool resizable = true;
    bool fullscreen = false;
    bool center = false;

    if (!g_gui_initialized) {
        hosc_gui_init();
    }

    if (options) {
        if (options->title) {
            title = options->title;
        }
        if (options->width > 0) {
            width = options->width;
        }
        if (options->height > 0) {
            height = options->height;
        }
        if (options->min_width > 0) {
            min_width = options->min_width;
        }
        if (options->min_height > 0) {
            min_height = options->min_height;
        }
        if (options->icon) {
            icon = options->icon;
        }
        resizable = options->resizable;
        fullscreen = options->fullscreen;
        center = options->center;
    }

#ifdef _WIN32
    if (g_gui_backend == HOSC_GUI_BACKEND_WIN32) {
        DWORD style = WS_OVERLAPPEDWINDOW;
        int x = CW_USEDEFAULT;
        int y = CW_USEDEFAULT;
        int show_cmd = SW_SHOW;

        if (fullscreen) {
            style = WS_POPUP | WS_VISIBLE;
            width = GetSystemMetrics(SM_CXSCREEN);
            height = GetSystemMetrics(SM_CYSCREEN);
            x = 0;
            y = 0;
            show_cmd = SW_MAXIMIZE;
        } else if (!resizable) {
            style = WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX;
        }

        if (g_gui_window) {
            DestroyWindow(g_gui_window);
            g_gui_window = NULL;
        }

        g_gui_min_width = min_width;
        g_gui_min_height = min_height;

        if (center && !fullscreen) {
            int screen_width = GetSystemMetrics(SM_CXSCREEN);
            int screen_height = GetSystemMetrics(SM_CYSCREEN);
            x = (screen_width - width) / 2;
            y = (screen_height - height) / 2;
            if (x < 0) {
                x = 0;
            }
            if (y < 0) {
                y = 0;
            }
        }

        g_gui_window = CreateWindowExA(
            0,
            HOSC_WINDOW_CLASS_NAME,
            title,
            style,
            x,
            y,
            width,
            height,
            NULL,
            NULL,
            GetModuleHandleA(NULL),
            NULL
        );

        if (g_gui_window) {
            hosc_apply_window_icon(g_gui_window, icon);
            hosc_ensure_backbuffer(width, height);
            ShowWindow(g_gui_window, show_cmd);
            UpdateWindow(g_gui_window);
            g_gui_running = true;
            hosc_gui_clear_event_queue();
            return true;
        }

        hosc_destroy_window_icon();
        g_gui_backend = HOSC_GUI_BACKEND_CONSOLE;
    }
#endif

    printf("[GUI:console] create_window title=\"%s\" size=%dx%d resizable=%s fullscreen=%s center=%s min=%dx%d icon=\"%s\"\n",
           title,
           width,
           height,
           (resizable ? "true" : "false"),
           (fullscreen ? "true" : "false"),
           (center ? "true" : "false"),
           min_width,
           min_height,
           (icon ? icon : ""));
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
        HDC dc = hosc_gui_begin_draw();
        if (dc) {
            TextOutA(dc, x, y, text, (int)strlen(text));
            hosc_gui_present();
        }
        hosc_gui_pump_events();
        return;
    }
#endif

    printf("[GUI:console] text x=%d y=%d msg=\"%s\"\n", x, y, text);
}

void hosc_gui_draw_text_styled(int x, int y, const char* text, int size, int r, int g, int b, bool bold) {
    if (!text) {
        text = "";
    }
    if (size <= 0) {
        size = 16;
    }

#ifdef _WIN32
    if (g_gui_backend == HOSC_GUI_BACKEND_WIN32 && g_gui_window) {
        HDC dc = hosc_gui_begin_draw();
        if (dc) {
            HFONT font;
            HGDIOBJ old_font;
            COLORREF old_color;
            int old_mode;

            font = CreateFontA(-size, 0, 0, 0,
                               (bold ? FW_BOLD : FW_NORMAL),
                               FALSE, FALSE, FALSE,
                               DEFAULT_CHARSET, OUT_DEFAULT_PRECIS,
                               CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                               DEFAULT_PITCH | FF_DONTCARE,
                               "Segoe UI");
            old_font = SelectObject(dc, font);
            old_color = SetTextColor(dc, RGB(r, g, b));
            old_mode = SetBkMode(dc, TRANSPARENT);
            TextOutA(dc, x, y, text, (int)strlen(text));
            SetBkMode(dc, old_mode);
            SetTextColor(dc, old_color);
            SelectObject(dc, old_font);
            DeleteObject(font);
            hosc_gui_present();
        }
        hosc_gui_pump_events();
        return;
    }
#endif

    printf("[GUI:console] text x=%d y=%d size=%d rgb=(%d,%d,%d) bold=%s msg=\"%s\"\n",
           x, y, size, r, g, b, (bold ? "true" : "false"), text);
}

void hosc_gui_draw_rect(int x, int y, int width, int height, int r, int g, int b, bool filled) {
#ifdef _WIN32
    if (g_gui_backend == HOSC_GUI_BACKEND_WIN32 && g_gui_window) {
        HDC dc = hosc_gui_begin_draw();
        if (dc) {
            RECT rect;
            HBRUSH brush;
            HPEN pen;
            HGDIOBJ old_brush;
            HGDIOBJ old_pen;

            rect.left = x;
            rect.top = y;
            rect.right = x + width;
            rect.bottom = y + height;

            brush = CreateSolidBrush(RGB(r, g, b));
            pen = CreatePen(PS_SOLID, 1, RGB(r, g, b));
            old_brush = SelectObject(dc, (filled ? brush : GetStockObject(NULL_BRUSH)));
            old_pen = SelectObject(dc, pen);
            Rectangle(dc, rect.left, rect.top, rect.right, rect.bottom);
            SelectObject(dc, old_brush);
            SelectObject(dc, old_pen);
            DeleteObject(brush);
            DeleteObject(pen);
            hosc_gui_present();
        }
        hosc_gui_pump_events();
        return;
    }
#endif

    printf("[GUI:console] rect x=%d y=%d size=%dx%d rgb=(%d,%d,%d) filled=%s\n",
           x, y, width, height, r, g, b, (filled ? "true" : "false"));
}

void hosc_gui_draw_round_rect(int x, int y, int width, int height, int radius,
                              int fill_r, int fill_g, int fill_b,
                              int border_r, int border_g, int border_b,
                              int border_width) {
#ifdef _WIN32
    if (g_gui_backend == HOSC_GUI_BACKEND_WIN32 && g_gui_window) {
        HDC dc = hosc_gui_begin_draw();
        if (dc) {
            HBRUSH brush;
            HPEN pen;
            HGDIOBJ old_brush;
            HGDIOBJ old_pen;
            int ellipse_width = radius * 2;
            int ellipse_height = radius * 2;

            if (ellipse_width <= 0) {
                ellipse_width = 2;
            }
            if (ellipse_height <= 0) {
                ellipse_height = 2;
            }

            brush = CreateSolidBrush(RGB(fill_r, fill_g, fill_b));
            if (border_width > 0) {
                pen = CreatePen(PS_SOLID, border_width, RGB(border_r, border_g, border_b));
            } else {
                pen = (HPEN)GetStockObject(NULL_PEN);
            }

            old_brush = SelectObject(dc, brush);
            old_pen = SelectObject(dc, pen);
            RoundRect(dc, x, y, x + width, y + height, ellipse_width, ellipse_height);
            SelectObject(dc, old_brush);
            SelectObject(dc, old_pen);
            DeleteObject(brush);
            if (border_width > 0) {
                DeleteObject(pen);
            }
            hosc_gui_present();
        }
        hosc_gui_pump_events();
        return;
    }
#endif

    printf("[GUI:console] round_rect x=%d y=%d size=%dx%d radius=%d fill=(%d,%d,%d) border=(%d,%d,%d) border_width=%d\n",
           x, y, width, height, radius,
           fill_r, fill_g, fill_b,
           border_r, border_g, border_b, border_width);
}

void hosc_gui_draw_image(int x, int y, int width, int height, const char* image_path) {
    if (!image_path) {
        image_path = "";
    }

#ifdef _WIN32
    if (g_gui_backend == HOSC_GUI_BACKEND_WIN32 && g_gui_window && image_path[0]) {
        char normalized_path[1024];
        WCHAR wide_path[1024];
        GpImage* image = NULL;
        GpGraphics* graphics = NULL;
        HDC dc = NULL;
        GpStatus status;

        if (width <= 0 || height <= 0) {
            printf("[GUI:win32] image skipped: width/height must be > 0\n");
            return;
        }

        if (!hosc_gdiplus_init()) {
            printf("[GUI:win32] image skipped: failed to initialize GDI+\n");
            return;
        }

        hosc_normalize_windows_path(image_path, normalized_path, sizeof(normalized_path));
        if (!hosc_utf8_to_wide(normalized_path, wide_path, (int)(sizeof(wide_path) / sizeof(wide_path[0])))) {
            printf("[GUI:win32] image skipped: invalid UTF-8 path \"%s\"\n", normalized_path);
            return;
        }

        status = GdipLoadImageFromFile(wide_path, &image);
        if (status != Ok || !image) {
            printf("[GUI:win32] image skipped: cannot load \"%s\"\n", normalized_path);
            return;
        }

        dc = hosc_gui_begin_draw();
        if (!dc) {
            GdipDisposeImage(image);
            return;
        }

        status = GdipCreateFromHDC(dc, &graphics);
        if (status == Ok && graphics) {
            status = GdipDrawImageRectI(graphics, image, x, y, width, height);
            if (status != Ok) {
                printf("[GUI:win32] image draw failed for \"%s\"\n", normalized_path);
            }
            GdipDeleteGraphics(graphics);
        }

        hosc_gui_present();
        GdipDisposeImage(image);
        hosc_gui_pump_events();
        return;
    }
#endif

    printf("[GUI:console] image x=%d y=%d size=%dx%d path=\"%s\"\n", x, y, width, height, image_path);
}

bool hosc_audio_play_file(const char* audio_path, bool async_play) {
    if (!audio_path || !audio_path[0]) {
        return false;
    }

#ifdef _WIN32
    {
        char normalized_path[1024];
        WCHAR wide_path[1024];
        HRESULT hr;

        hosc_audio_stop_internal();
        hosc_normalize_windows_path(audio_path, normalized_path, sizeof(normalized_path));
        if (!hosc_utf8_to_wide(normalized_path, wide_path, (int)(sizeof(wide_path) / sizeof(wide_path[0])))) {
            fprintf(stderr, "[audio] invalid UTF-8 path: %s\n", audio_path);
            return false;
        }

        if (!hosc_media_foundation_init()) {
            fprintf(stderr, "[audio] failed to initialize Media Foundation\n");
            return false;
        }

        hr = MFPCreateMediaPlayer(wide_path,
                                  async_play ? TRUE : FALSE,
                                  MFP_OPTION_FREE_THREADED_CALLBACK,
                                  &g_hosc_media_callback.iface,
                                  NULL,
                                  &g_audio_player);
        if (FAILED(hr) || !g_audio_player) {
            fprintf(stderr, "[audio] MFPlay open failed for \"%s\" (hr=0x%08lx)\n",
                    normalized_path, (unsigned long)hr);
            return false;
        }

        g_audio_internal_playback = true;
        g_audio_ready = true;
        strncpy(g_last_audio_path, normalized_path, sizeof(g_last_audio_path) - 1);
        g_last_audio_path[sizeof(g_last_audio_path) - 1] = '\0';
        if (!async_play) {
            g_audio_player->lpVtbl->Play(g_audio_player);
        }
        return true;
    }
#else
    printf("[audio unavailable] %s\n", audio_path);
    (void)async_play;
    return false;
#endif
}

void hosc_audio_stop(void) {
#ifdef _WIN32
    hosc_audio_stop_internal();
#endif
}

bool hosc_gui_pick_audio_file(char* out_path, size_t out_cap) {
#ifdef _WIN32
    OPENFILENAMEW dialog;
    WCHAR file_buffer[MAX_PATH];

    if (!out_path || out_cap == 0) {
        return false;
    }

    memset(file_buffer, 0, sizeof(file_buffer));
    memset(&dialog, 0, sizeof(dialog));
    dialog.lStructSize = sizeof(dialog);
    dialog.hwndOwner = g_gui_window;
    dialog.lpstrFile = file_buffer;
    dialog.nMaxFile = (DWORD)sizeof(file_buffer);
    dialog.lpstrFilter = L"Audio Files\0*.mp3;*.wav;*.ogg;*.flac;*.m4a\0All Files\0*.*\0";
    dialog.nFilterIndex = 1;
    dialog.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_HIDEREADONLY;
    dialog.lpstrTitle = L"Select audio file";

    if (!GetOpenFileNameW(&dialog)) {
        return false;
    }

    return hosc_wide_to_utf8(file_buffer, out_path, (int)out_cap);
#else
    (void)out_path;
    (void)out_cap;
    return false;
#endif
}

bool hosc_audio_has_internal_playback(void) {
#ifdef _WIN32
    return (g_audio_internal_playback && g_audio_player != NULL);
#else
    return false;
#endif
}

int hosc_audio_get_position_ms(void) {
#ifdef _WIN32
    PROPVARIANT value;

    if (!hosc_audio_has_internal_playback()) {
        return -1;
    }

    PropVariantInit(&value);
    if (FAILED(g_audio_player->lpVtbl->GetPosition(g_audio_player, &MFP_POSITIONTYPE_100NS, &value))) {
        return -1;
    }

    if (value.vt != VT_I8 && value.vt != VT_UI8) {
        PropVariantClear(&value);
        return -1;
    }

    {
        LONGLONG raw_value = (value.vt == VT_I8 ? value.hVal.QuadPart : (LONGLONG)value.uhVal.QuadPart);
        int ms = (int)(raw_value / 10000LL);
        PropVariantClear(&value);
        return ms;
    }
#else
    return -1;
#endif
}

int hosc_audio_get_duration_ms(void) {
#ifdef _WIN32
    PROPVARIANT value;

    if (!hosc_audio_has_internal_playback()) {
        return -1;
    }

    PropVariantInit(&value);
    if (FAILED(g_audio_player->lpVtbl->GetDuration(g_audio_player, &MFP_POSITIONTYPE_100NS, &value))) {
        return -1;
    }

    if (value.vt != VT_I8 && value.vt != VT_UI8) {
        PropVariantClear(&value);
        return -1;
    }

    {
        LONGLONG raw_value = (value.vt == VT_I8 ? value.hVal.QuadPart : (LONGLONG)value.uhVal.QuadPart);
        int ms = (int)(raw_value / 10000LL);
        PropVariantClear(&value);
        return ms;
    }
#else
    return -1;
#endif
}

bool hosc_audio_seek_ms(int position_ms) {
#ifdef _WIN32
    PROPVARIANT value;
    int duration_ms;

    if (!hosc_audio_has_internal_playback()) {
        return false;
    }

    duration_ms = hosc_audio_get_duration_ms();
    if (duration_ms > 0) {
        if (position_ms < 0) {
            position_ms = 0;
        }
        if (position_ms > duration_ms) {
            position_ms = duration_ms;
        }
    }

    PropVariantInit(&value);
    value.vt = VT_I8;
    value.hVal.QuadPart = (LONGLONG)position_ms * 10000LL;
    if (FAILED(g_audio_player->lpVtbl->SetPosition(g_audio_player, &MFP_POSITIONTYPE_100NS, &value))) {
        return false;
    }

    if (FAILED(g_audio_player->lpVtbl->Play(g_audio_player))) {
        return false;
    }

    return true;
#else
    (void)position_ms;
    return false;
#endif
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




