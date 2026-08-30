/*
 * File: framework\src\hosc_runtime.c
 * Purpose: HOSC source file - Dynamic GUI backend implementation
 */

#ifndef WINVER
#define WINVER 0x0601
#endif
#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0601
#endif
#ifndef _POSIX_C_SOURCE
#define _POSIX_C_SOURCE 200809L
#endif

#include "hosc_runtime.h"
#include "runtime_gui_backend.h"

#include <ctype.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef COBJMACROS
#define COBJMACROS
#endif
#ifndef CINTERFACE
#define CINTERFACE
#endif
#include <windows.h>
#include <commdlg.h>
#include <mfapi.h>
#include <mfplay.h>
#include <propidl.h>

/* GDI+ C declarations for cross-compiler compatibility (MSVC & GCC) */
typedef enum {
    GdiplusStatusOk = 0
} GpStatus;

typedef struct {
    UINT32 GdiplusVersion;
    void* DebugEventCallback;
    BOOL SuppressBackgroundThread;
    BOOL SuppressExternalCodecs;
} GdiplusStartupInput;

typedef void GpImage;
typedef void GpGraphics;

#define Ok GdiplusStatusOk

#ifndef WINGDIPAPI
#define WINGDIPAPI __stdcall
#endif

#ifdef __cplusplus
extern "C" {
#endif
GpStatus WINGDIPAPI GdiplusStartup(ULONG_PTR *token, const GdiplusStartupInput *input, void *output);
void WINGDIPAPI GdiplusShutdown(ULONG_PTR token);
GpStatus WINGDIPAPI GdipLoadImageFromFile(const WCHAR* filename, GpImage **image);
GpStatus WINGDIPAPI GdipCreateFromHDC(HDC hdc, GpGraphics **graphics);
GpStatus WINGDIPAPI GdipDrawImageRectI(GpGraphics *graphics, GpImage *image, INT x, INT y, INT width, INT height);
GpStatus WINGDIPAPI GdipDeleteGraphics(GpGraphics *graphics);
GpStatus WINGDIPAPI GdipDisposeImage(GpImage *image);
#ifdef __cplusplus
}
#endif
#else
#include <fcntl.h>
#include <signal.h>
#include <sys/wait.h>
#include <unistd.h>
#if defined(HOSC_HAVE_LIBMPV)
#include <mpv/client.h>
#elif defined(HOSC_HAVE_LIBVLC)
#include <vlc/vlc.h>
#endif
#endif

// ============================================================================
// INTERNAL UTILITIES
// ============================================================================

static char g_runtime_base_dir[1024] = {0};

static bool hosc_path_is_absolute(const char* input) {
    if (!input || !input[0]) return false;
#ifdef _WIN32
    if ((isalpha((unsigned char)input[0]) && input[1] == ':') || (input[0] == '\\' && input[1] == '\\')) return true;
#endif
    return input[0] == '/' || input[0] == '\\';
}

/* See declaration/rationale in services/include/runtime_gui_backend.h.
 * Resolves relative to the script's base dir (captured at launch, see
 * hosc_runtime_set_base_dir), but only after checking whether the path
 * already resolves as-is from the current working directory - most
 * existing .hosc scripts (and the ones in framework/examples/) write
 * asset paths assuming "run from the repo root", so this keeps those
 * working unchanged while also fixing the case where the tool is invoked
 * from a different directory. Nothing here is specific to any one script
 * or asset - it applies uniformly to icons, cover art, and audio. */
bool hosc_runtime_resolve_path(const char* input, char* output, size_t output_cap) {
    if (!output || output_cap == 0) return false;
    output[0] = '\0';
    if (!input || !input[0]) return false;

    if (hosc_path_is_absolute(input) || !g_runtime_base_dir[0]) {
        strncpy(output, input, output_cap - 1);
        output[output_cap - 1] = '\0';
        return true;
    }

    {
        FILE* probe = fopen(input, "rb");
        if (probe) {
            fclose(probe);
            strncpy(output, input, output_cap - 1);
            output[output_cap - 1] = '\0';
            return true;
        }
    }

    {
        size_t base_len = strlen(g_runtime_base_dir);
        bool need_sep = base_len > 0 &&
            g_runtime_base_dir[base_len - 1] != '/' &&
            g_runtime_base_dir[base_len - 1] != '\\';
        snprintf(output, output_cap, "%s%s%s", g_runtime_base_dir, need_sep ? "/" : "", input);
    }
    return true;
}

static uint64_t hosc_now_ms(void) {
#ifdef _WIN32
    return (uint64_t)GetTickCount64();
#else
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)(ts.tv_sec * 1000ULL + ts.tv_nsec / 1000000ULL);
#endif
}

static char* hosc_strdup_local(const char* input) {
    size_t len;
    char* out;
    if (!input) return NULL;
    len = strlen(input);
    out = (char*)malloc(len + 1);
    if (!out) return NULL;
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

static size_t memory_header_size(void) { return sizeof(size_t); }

static void memory_track_alloc(size_t sz) {
    g_memory_stats.total_allocated += sz;
    g_memory_stats.current_in_use += sz;
    if (g_memory_stats.current_in_use > g_memory_stats.peak_usage)
        g_memory_stats.peak_usage = g_memory_stats.current_in_use;
    g_memory_stats.allocation_count++;
}

static void memory_track_free(size_t sz) {
    g_memory_stats.total_deallocated += sz;
    if (g_memory_stats.current_in_use >= sz)
        g_memory_stats.current_in_use -= sz;
    else
        g_memory_stats.current_in_use = 0;
}

static void* memory_allocate(size_t size) {
    size_t header = memory_header_size();
    uint8_t* raw = (uint8_t*)malloc(header + size);
    if (!raw) return NULL;
    memcpy(raw, &size, sizeof(size));
    if (g_memory_stats.tracking_enabled) memory_track_alloc(size);
    return raw + header;
}

static void* memory_reallocate(void* ptr, size_t new_size) {
    size_t header = memory_header_size();
    size_t old_size = 0;
    uint8_t* raw_ptr;
    uint8_t* new_raw;
    if (!ptr) return memory_allocate(new_size);
    raw_ptr = ((uint8_t*)ptr) - header;
    memcpy(&old_size, raw_ptr, sizeof(old_size));
    new_raw = (uint8_t*)realloc(raw_ptr, header + new_size);
    if (!new_raw) return NULL;
    memcpy(new_raw, &new_size, sizeof(new_size));
    if (g_memory_stats.tracking_enabled) { memory_track_free(old_size); memory_track_alloc(new_size); }
    return new_raw + header;
}

static void memory_deallocate(void* ptr) {
    size_t header = memory_header_size();
    size_t size = 0;
    uint8_t* raw_ptr;
    if (!ptr) return;
    raw_ptr = ((uint8_t*)ptr) - header;
    memcpy(&size, raw_ptr, sizeof(size));
    if (g_memory_stats.tracking_enabled) memory_track_free(size);
    free(raw_ptr);
}

static size_t memory_get_allocated_size(void* ptr) {
    size_t header = memory_header_size();
    size_t size = 0;
    if (!ptr) return 0;
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
// GUI BACKEND - Dynamic backend dispatch
// ============================================================================

#define HOSC_GUI_EVENT_QUEUE_CAP 256

static HOSCGUIBackend g_gui_backend = HOSC_GUI_BACKEND_CONSOLE;
static bool g_gui_initialized = false;
static bool g_gui_running = false;
static HOSCGUIEvent g_gui_event_queue[HOSC_GUI_EVENT_QUEUE_CAP];
static size_t g_gui_event_head = 0;
static size_t g_gui_event_tail = 0;

/* Currently active backend and its window */
static const HVM_GuiBackend* g_active_backend = NULL;
static HVM_GuiBackendWindow* g_active_window = NULL;

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
    if (hosc_gui_event_queue_is_full())
        g_gui_event_head = (g_gui_event_head + 1U) % HOSC_GUI_EVENT_QUEUE_CAP;
    g_gui_event_queue[g_gui_event_tail] = event;
    g_gui_event_tail = (g_gui_event_tail + 1U) % HOSC_GUI_EVENT_QUEUE_CAP;
}

static bool hosc_gui_pop_event(HOSCGUIEvent* out_event) {
    if (g_gui_event_head == g_gui_event_tail) return false;
    if (out_event) *out_event = g_gui_event_queue[g_gui_event_head];
    g_gui_event_head = (g_gui_event_head + 1U) % HOSC_GUI_EVENT_QUEUE_CAP;
    return true;
}

#ifdef _WIN32
/* Win32-specific state for audio and legacy drawing */
static HWND g_gui_window = NULL;
static HDC g_gui_backbuffer_dc = NULL;
static HBITMAP g_gui_backbuffer_bitmap = NULL;
static HBITMAP g_gui_backbuffer_old_bitmap = NULL;
static int g_gui_backbuffer_width = 0;
static int g_gui_backbuffer_height = 0;
static int g_gui_present_suspend_count = 0;
static const char* HOSC_WINDOW_CLASS_NAME = "HOSCFrameworkWindowClass";
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

/* Win32 audio/icon/window helpers */
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
    { (IMFPMediaPlayerCallbackVtbl*)&g_hosc_media_callback_vtbl }, 1
};

static const IID HOSC_IID_IUNKNOWN = {0x00000000, 0x0000, 0x0000, {0xc0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x46}};
static const IID HOSC_IID_IMFPMEDIAPLAYERCALLBACK = {0x766c8ffb, 0x5fdb, 0x4fea, {0xa2, 0x8d, 0xb9, 0x12, 0x99, 0x6f, 0x51, 0xbd}};

static int hosc_lparam_x(LPARAM v) { return (int)(short)LOWORD(v); }
static int hosc_lparam_y(LPARAM v) { return (int)(short)HIWORD(v); }

static HRESULT STDMETHODCALLTYPE hosc_media_callback_query_interface(IMFPMediaPlayerCallback* self, REFIID riid, void** out_object) {
    if (!out_object) return E_POINTER;
    *out_object = NULL;
    if (InlineIsEqualGUID(riid, &HOSC_IID_IUNKNOWN) || InlineIsEqualGUID(riid, &HOSC_IID_IMFPMEDIAPLAYERCALLBACK)) {
        *out_object = self; hosc_media_callback_add_ref(self); return S_OK;
    }
    return E_NOINTERFACE;
}

static ULONG STDMETHODCALLTYPE hosc_media_callback_add_ref(IMFPMediaPlayerCallback* self) {
    return (ULONG)InterlockedIncrement(&((HOSCMediaPlayerCallback*)self)->ref_count);
}
static ULONG STDMETHODCALLTYPE hosc_media_callback_release(IMFPMediaPlayerCallback* self) {
    LONG v = InterlockedDecrement(&((HOSCMediaPlayerCallback*)self)->ref_count);
    if (v < 1) { ((HOSCMediaPlayerCallback*)self)->ref_count = 1; v = 1; }
    return (ULONG)v;
}

static void STDMETHODCALLTYPE hosc_media_callback_on_event(IMFPMediaPlayerCallback* self, MFP_EVENT_HEADER* event_header) {
    (void)self;
    if (!event_header) return;
    if (FAILED(event_header->hrEvent)) { g_audio_ready = false; return; }
    switch (event_header->eEventType) {
        case MFP_EVENT_TYPE_MEDIAITEM_CREATED: case MFP_EVENT_TYPE_MEDIAITEM_SET:
        case MFP_EVENT_TYPE_PLAY: case MFP_EVENT_TYPE_POSITION_SET:
            g_audio_ready = true; break;
        case MFP_EVENT_TYPE_MEDIAITEM_CLEARED: case MFP_EVENT_TYPE_ERROR:
            g_audio_ready = false; break;
        case MFP_EVENT_TYPE_PLAYBACK_ENDED: g_audio_ready = true; break;
        default: break;
    }
}

static void hosc_destroy_window_icon(void) {
    if (g_gui_window_icon) { DestroyIcon(g_gui_window_icon); g_gui_window_icon = NULL; }
}

static void hosc_release_backbuffer(void) {
    if (g_gui_backbuffer_dc) {
        if (g_gui_backbuffer_old_bitmap) { SelectObject(g_gui_backbuffer_dc, g_gui_backbuffer_old_bitmap); g_gui_backbuffer_old_bitmap = NULL; }
        if (g_gui_backbuffer_bitmap) { DeleteObject(g_gui_backbuffer_bitmap); g_gui_backbuffer_bitmap = NULL; }
        DeleteDC(g_gui_backbuffer_dc); g_gui_backbuffer_dc = NULL;
    }
    g_gui_backbuffer_width = 0; g_gui_backbuffer_height = 0;
}

static bool hosc_ensure_backbuffer(int width, int height) {
    HDC window_dc; RECT rect; HBRUSH brush;
    if (!g_gui_window) return false;
    if (width <= 0 || height <= 0) {
        if (!GetClientRect(g_gui_window, &rect)) return false;
        width = rect.right - rect.left; height = rect.bottom - rect.top;
    }
    if (width <= 0 || height <= 0) return false;
    if (g_gui_backbuffer_dc && g_gui_backbuffer_width == width && g_gui_backbuffer_height == height) return true;
    hosc_release_backbuffer();
    window_dc = GetDC(g_gui_window);
    if (!window_dc) return false;
    g_gui_backbuffer_dc = CreateCompatibleDC(window_dc);
    if (!g_gui_backbuffer_dc) { ReleaseDC(g_gui_window, window_dc); return false; }
    g_gui_backbuffer_bitmap = CreateCompatibleBitmap(window_dc, width, height);
    ReleaseDC(g_gui_window, window_dc);
    if (!g_gui_backbuffer_bitmap) { hosc_release_backbuffer(); return false; }
    g_gui_backbuffer_old_bitmap = (HBITMAP)SelectObject(g_gui_backbuffer_dc, g_gui_backbuffer_bitmap);
    g_gui_backbuffer_width = width; g_gui_backbuffer_height = height;
    brush = CreateSolidBrush(RGB(255, 255, 255));
    if (brush) {
        RECT fr = {0, 0, width, height};
        FillRect(g_gui_backbuffer_dc, &fr, brush); DeleteObject(brush);
    }
    return true;
}

static HDC hosc_gui_begin_draw(void) {
    if (g_gui_backend != HOSC_GUI_BACKEND_WIN32 || !g_gui_window) return NULL;
    if (!hosc_ensure_backbuffer(0, 0)) return NULL;
    return g_gui_backbuffer_dc;
}

static void hosc_gui_present(void) {
    HDC window_dc;
    if (!g_gui_window || !g_gui_backbuffer_dc || g_gui_backbuffer_width <= 0 || g_gui_backbuffer_height <= 0) return;
    if (g_gui_present_suspend_count > 0) return;
    window_dc = GetDC(g_gui_window);
    if (!window_dc) return;
    BitBlt(window_dc, 0, 0, g_gui_backbuffer_width, g_gui_backbuffer_height, g_gui_backbuffer_dc, 0, 0, SRCCOPY);
    ReleaseDC(g_gui_window, window_dc);
}

static void hosc_apply_window_icon(HWND hwnd, const char* icon_path) {
    HICON icon; char normalized_path[1024]; WCHAR wide_path[1024];
    hosc_destroy_window_icon();
    if (!icon_path || !*icon_path) return;
    hosc_normalize_windows_path(icon_path, normalized_path, sizeof(normalized_path));
    if (!hosc_utf8_to_wide(normalized_path, wide_path, (int)(sizeof(wide_path) / sizeof(wide_path[0])))) return;
    icon = (HICON)LoadImageW(NULL, wide_path, IMAGE_ICON, 0, 0, LR_LOADFROMFILE | LR_DEFAULTSIZE);
    if (!icon) return;
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
                if (hosc_ensure_backbuffer(0, 0) && g_gui_backbuffer_dc)
                    BitBlt(dc, 0, 0, g_gui_backbuffer_width, g_gui_backbuffer_height, g_gui_backbuffer_dc, 0, 0, SRCCOPY);
                EndPaint(hwnd, &ps);
            }
            return 0;
        }
        case WM_ERASEBKGND: return 1;
        case WM_SIZE: hosc_ensure_backbuffer(LOWORD(lparam), HIWORD(lparam)); return 0;
        case WM_CLOSE: hosc_gui_push_event(HOSC_GUI_EVENT_QUIT, 0, 0, 0, 0); DestroyWindow(hwnd); return 0;
        case WM_DESTROY:
            g_gui_running = false; hosc_release_backbuffer(); g_gui_window = NULL;
            hosc_gui_push_event(HOSC_GUI_EVENT_QUIT, 0, 0, 0, 0); return 0;
        case WM_GETMINMAXINFO: {
            MINMAXINFO* mm = (MINMAXINFO*)lparam;
            if (g_gui_min_width > 0) mm->ptMinTrackSize.x = g_gui_min_width;
            if (g_gui_min_height > 0) mm->ptMinTrackSize.y = g_gui_min_height;
            return 0;
        }
        case WM_KEYDOWN: hosc_gui_push_event(HOSC_GUI_EVENT_KEY_DOWN, (int)wparam, 0, 0, 0); return 0;
        case WM_KEYUP: hosc_gui_push_event(HOSC_GUI_EVENT_KEY_UP, (int)wparam, 0, 0, 0); return 0;
        case WM_MOUSEMOVE: hosc_gui_push_event(HOSC_GUI_EVENT_MOUSE_MOVE, 0, hosc_lparam_x(lparam), hosc_lparam_y(lparam), 0); return 0;
        case WM_LBUTTONDOWN: hosc_gui_push_event(HOSC_GUI_EVENT_MOUSE_DOWN, 0, hosc_lparam_x(lparam), hosc_lparam_y(lparam), 1); return 0;
        case WM_LBUTTONUP: hosc_gui_push_event(HOSC_GUI_EVENT_MOUSE_UP, 0, hosc_lparam_x(lparam), hosc_lparam_y(lparam), 1); return 0;
        case WM_RBUTTONDOWN: hosc_gui_push_event(HOSC_GUI_EVENT_MOUSE_DOWN, 0, hosc_lparam_x(lparam), hosc_lparam_y(lparam), 2); return 0;
        case WM_RBUTTONUP: hosc_gui_push_event(HOSC_GUI_EVENT_MOUSE_UP, 0, hosc_lparam_x(lparam), hosc_lparam_y(lparam), 2); return 0;
        case WM_MBUTTONDOWN: hosc_gui_push_event(HOSC_GUI_EVENT_MOUSE_DOWN, 0, hosc_lparam_x(lparam), hosc_lparam_y(lparam), 3); return 0;
        case WM_MBUTTONUP: hosc_gui_push_event(HOSC_GUI_EVENT_MOUSE_UP, 0, hosc_lparam_x(lparam), hosc_lparam_y(lparam), 3); return 0;
        default: return DefWindowProcA(hwnd, msg, wparam, lparam);
    }
}

static bool hosc_register_window_class(void) {
    WNDCLASSA wc;
    if (g_gui_class_registered) return true;
    memset(&wc, 0, sizeof(wc));
    wc.lpfnWndProc = hosc_window_proc;
    wc.hInstance = GetModuleHandleA(NULL);
    wc.lpszClassName = HOSC_WINDOW_CLASS_NAME;
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);
    wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
    if (!RegisterClassA(&wc)) return false;
    g_gui_class_registered = true;
    return true;
}

static bool hosc_gdiplus_init(void) {
    GdiplusStartupInput si;
    if (g_gdiplus_started) return true;
    memset(&si, 0, sizeof(si)); si.GdiplusVersion = 1;
    if (GdiplusStartup(&g_gdiplus_token, &si, NULL) != Ok) { g_gdiplus_token = 0; return false; }
    g_gdiplus_started = true;
    return true;
}

static void hosc_gdiplus_shutdown(void) {
    if (g_gdiplus_started) { GdiplusShutdown(g_gdiplus_token); g_gdiplus_token = 0; g_gdiplus_started = false; }
}

static int hosc_utf8_to_wide(const char* input, WCHAR* output, int output_count) {
    if (!input || !output || output_count <= 0) return 0;
    return MultiByteToWideChar(CP_UTF8, 0, input, -1, output, output_count);
}

static int hosc_wide_to_utf8(const WCHAR* input, char* output, int output_count) {
    if (!input || !output || output_count <= 0) return 0;
    return WideCharToMultiByte(CP_UTF8, 0, input, -1, output, output_count, NULL, NULL);
}

static int hosc_is_absolute_path(const char* input) {
    if (!input || !input[0]) return 0;
    if ((isalpha((unsigned char)input[0]) && input[1] == ':') || (input[0] == '\\' && input[1] == '\\') || input[0] == '/' || input[0] == '\\') return 1;
    return 0;
}

static void hosc_normalize_windows_path(const char* input, char* output, size_t output_cap) {
    char candidate[2048]; WCHAR wc[2048], wf[2048]; DWORD len; size_t i;
    if (!input || !output || output_cap == 0) return;
    output[0] = '\0';
    if (!hosc_is_absolute_path(input) && g_runtime_base_dir[0])
        snprintf(candidate, sizeof(candidate), "%s\\%s", g_runtime_base_dir, input);
    else { strncpy(candidate, input, sizeof(candidate)-1); candidate[sizeof(candidate)-1] = '\0'; }
    if (!hosc_utf8_to_wide(candidate, wc, (int)(sizeof(wc)/sizeof(wc[0])))) { strncpy(output, candidate, output_cap-1); output[output_cap-1] = '\0'; return; }
    len = GetFullPathNameW(wc, (DWORD)(sizeof(wf)/sizeof(wf[0])), wf, NULL);
    if (len > 0 && len < (DWORD)(sizeof(wf)/sizeof(wf[0])) && hosc_wide_to_utf8(wf, output, (int)output_cap)) output[output_cap-1] = '\0';
    else { strncpy(output, candidate, output_cap-1); output[output_cap-1] = '\0'; }
    for (i = 0; output[i]; i++) if (output[i] == '/') output[i] = '\\';
}

static void hosc_audio_stop_internal(void) {
    if (g_audio_player) { g_audio_player->lpVtbl->Stop(g_audio_player); g_audio_player->lpVtbl->Shutdown(g_audio_player); g_audio_player->lpVtbl->Release(g_audio_player); g_audio_player = NULL; }
    g_audio_internal_playback = false; g_audio_ready = false;
}

static bool hosc_media_foundation_init(void) {
    HRESULT hr;
    if (g_media_foundation_started) return true;
    hr = CoInitializeEx(NULL, COINIT_APARTMENTTHREADED);
    if (SUCCEEDED(hr) || hr == RPC_E_CHANGED_MODE) { if (SUCCEEDED(hr)) g_com_initialized = true; }
    else return false;
    hr = MFStartup(MF_VERSION, MFSTARTUP_FULL);
    if (FAILED(hr)) { if (g_com_initialized) { CoUninitialize(); g_com_initialized = false; } return false; }
    g_media_foundation_started = true;
    return true;
}

static void hosc_media_foundation_shutdown(void) {
    hosc_audio_stop_internal();
    if (g_media_foundation_started) { MFShutdown(); g_media_foundation_started = false; }
    if (g_com_initialized) { CoUninitialize(); g_com_initialized = false; }
}
#endif /* _WIN32 */

#ifndef _WIN32
/*
 * POSIX (Linux/macOS) audio backend for the GUI framework runtime.
 *
 * There is no bundled decoder (SDL2/OpenAL/PulseAudio/PipeWire aren't linked
 * yet), so playback is delegated to whichever command-line media player is
 * already on PATH, run as a detached child process so audio.play() stays
 * non-blocking (matching async_play=true, which is how the framework always
 * calls this). Position is estimated from wall-clock elapsed time since the
 * child was spawned; duration is queried once via ffprobe if available.
 */

#if defined(HOSC_HAVE_LIBMPV)
/* --------------------------------------------------------------------
 * libmpv backend - preferred when available. Gives real position,
 * duration, and seeking directly from the library instead of estimating
 * from wall-clock time or shelling out to a CLI player.
 * ------------------------------------------------------------------ */
static mpv_handle* g_mpv = NULL;
static char g_last_audio_path[1024] = {0};

static bool hosc_mpv_ensure(void) {
    if (g_mpv) return true;
    g_mpv = mpv_create();
    if (!g_mpv) return false;
    /* Headless: no video window, no terminal input grabbing. Note:
     * "terminal"="no" only stops mpv from reading stdin/using its own
     * console UI - it does NOT silence mpv_request_log_messages below,
     * which is how we surface real errors (bad file, no audio device,
     * missing codec, ...) instead of failing silently. */
    mpv_set_option_string(g_mpv, "vid", "no");
    mpv_set_option_string(g_mpv, "terminal", "no");
    mpv_set_option_string(g_mpv, "input-default-bindings", "no");
    mpv_set_option_string(g_mpv, "input-vo-keyboard", "no");
    mpv_set_option_string(g_mpv, "audio-display", "no");
    if (mpv_initialize(g_mpv) < 0) {
        mpv_terminate_destroy(g_mpv);
        g_mpv = NULL;
        return false;
    }
    /* "warn" gets us audio-output-init failures, missing-codec errors,
     * bad-file errors, etc. without flooding stderr with info-level chatter. */
    mpv_request_log_messages(g_mpv, "warn");
    return true;
}

/* Drains and prints any pending mpv log messages / end-of-file errors.
 * Cheap (non-blocking, 0-timeout poll) - safe to call every frame or on
 * every audio query, and is the only way real playback failures (e.g. "no
 * audio device", "unsupported codec") become visible instead of the call
 * just silently doing nothing. */
static void hosc_mpv_drain_log(void) {
    if (!g_mpv) return;
    for (;;) {
        mpv_event* ev = mpv_wait_event(g_mpv, 0.0);
        if (!ev || ev->event_id == MPV_EVENT_NONE) break;
        if (ev->event_id == MPV_EVENT_LOG_MESSAGE && ev->data) {
            mpv_event_log_message* msg = (mpv_event_log_message*)ev->data;
            fprintf(stderr, "[mpv:%s] %s", msg->level, msg->text);
        } else if (ev->event_id == MPV_EVENT_END_FILE && ev->data) {
            mpv_event_end_file* ef = (mpv_event_end_file*)ev->data;
            if (ef->reason == MPV_END_FILE_REASON_ERROR) {
                fprintf(stderr, "[audio] mpv playback failed for \"%s\": %s\n",
                        g_last_audio_path, mpv_error_string(ef->error));
            }
        }
    }
}

static void hosc_audio_stop_internal(void) {
    if (g_mpv) {
        const char* cmd[] = {"stop", NULL};
        mpv_command(g_mpv, cmd);
    }
}

static void hosc_mpv_shutdown(void) {
    if (g_mpv) {
        mpv_terminate_destroy(g_mpv);
        g_mpv = NULL;
    }
}
#elif defined(HOSC_HAVE_LIBVLC)
/* --------------------------------------------------------------------
 * libvlc backend - used when libmpv isn't available but libvlc is.
 * ------------------------------------------------------------------ */
static libvlc_instance_t* g_vlc = NULL;
static libvlc_media_player_t* g_vlc_player = NULL;
static char g_last_audio_path[1024] = {0};

static bool hosc_vlc_ensure(void) {
    if (g_vlc) return true;
    {
        const char* args[] = {"--no-video", "--quiet", "--no-xlib"};
        g_vlc = libvlc_new(3, args);
    }
    return g_vlc != NULL;
}

static void hosc_audio_stop_internal(void) {
    if (g_vlc_player) {
        libvlc_media_player_stop(g_vlc_player);
        libvlc_media_player_release(g_vlc_player);
        g_vlc_player = NULL;
    }
}

static void hosc_vlc_shutdown(void) {
    hosc_audio_stop_internal();
    if (g_vlc) {
        libvlc_release(g_vlc);
        g_vlc = NULL;
    }
}
#else
typedef enum {
    HOSC_AUDIO_PLAYER_NONE = 0,
    HOSC_AUDIO_PLAYER_FFPLAY,
    HOSC_AUDIO_PLAYER_MPV,
    HOSC_AUDIO_PLAYER_CVLC,
    HOSC_AUDIO_PLAYER_MPG123,
    HOSC_AUDIO_PLAYER_PAPLAY,
    HOSC_AUDIO_PLAYER_APLAY,
    HOSC_AUDIO_PLAYER_AFPLAY
} HoscAudioPlayerKind;

static pid_t g_audio_child_pid = -1;
static bool g_audio_playing = false;
static char g_last_audio_path[1024] = {0};
static HoscAudioPlayerKind g_audio_player_kind = HOSC_AUDIO_PLAYER_NONE;
static struct timespec g_audio_start_ts;
static long g_audio_seek_offset_ms = 0;
static int g_audio_duration_ms_cache = -1;
static bool g_audio_sigchld_ignored = false;

static bool hosc_audio_find_in_path(const char* name) {
    const char* path_env = getenv("PATH");
    char buf[1024];
    const char* start;
    const char* colon;
    size_t name_len;

    if (!path_env || !path_env[0]) return false;
    name_len = strlen(name);
    start = path_env;

    while (*start) {
        size_t dir_len;
        colon = strchr(start, ':');
        dir_len = colon ? (size_t)(colon - start) : strlen(start);
        if (dir_len > 0 && dir_len + 1 + name_len < sizeof(buf)) {
            memcpy(buf, start, dir_len);
            buf[dir_len] = '/';
            memcpy(buf + dir_len + 1, name, name_len + 1);
            if (access(buf, X_OK) == 0) return true;
        }
        if (!colon) break;
        start = colon + 1;
    }
    return false;
}

static HoscAudioPlayerKind hosc_audio_detect_player(void) {
#ifdef __APPLE__
    if (hosc_audio_find_in_path("afplay")) return HOSC_AUDIO_PLAYER_AFPLAY;
#endif
    if (hosc_audio_find_in_path("ffplay")) return HOSC_AUDIO_PLAYER_FFPLAY;
    if (hosc_audio_find_in_path("mpv")) return HOSC_AUDIO_PLAYER_MPV;
    if (hosc_audio_find_in_path("cvlc")) return HOSC_AUDIO_PLAYER_CVLC;
    if (hosc_audio_find_in_path("mpg123")) return HOSC_AUDIO_PLAYER_MPG123;
    if (hosc_audio_find_in_path("paplay")) return HOSC_AUDIO_PLAYER_PAPLAY;
    if (hosc_audio_find_in_path("aplay")) return HOSC_AUDIO_PLAYER_APLAY;
    return HOSC_AUDIO_PLAYER_NONE;
}

static void hosc_audio_reap_if_finished(void) {
    int status;
    pid_t r;
    if (!g_audio_playing || g_audio_child_pid <= 0) return;
    r = waitpid(g_audio_child_pid, &status, WNOHANG);
    if (r == g_audio_child_pid) {
        g_audio_playing = false;
        g_audio_child_pid = -1;
    }
}

static void hosc_audio_stop_internal(void) {
    if (g_audio_child_pid > 0) {
        kill(g_audio_child_pid, SIGTERM);
        waitpid(g_audio_child_pid, NULL, 0);
    }
    g_audio_child_pid = -1;
    g_audio_playing = false;
}

/* Builds argv for `kind` playing `path`, optionally starting `start_seconds`
 * into the file. Players without a simple CLI seek flag (mpg123, the PCM
 * fallbacks, afplay) ignore start_seconds and always start from 0. */
static void hosc_audio_build_argv(HoscAudioPlayerKind kind, const char* path, double start_seconds, char* argv_storage[16], char secbuf[64]) {
    int i = 0;
    if (start_seconds > 0.0) snprintf(secbuf, 64, "%.3f", start_seconds);

    switch (kind) {
        case HOSC_AUDIO_PLAYER_FFPLAY:
            argv_storage[i++] = "ffplay"; argv_storage[i++] = "-nodisp"; argv_storage[i++] = "-autoexit"; argv_storage[i++] = "-loglevel"; argv_storage[i++] = "quiet";
            if (start_seconds > 0.0) { argv_storage[i++] = "-ss"; argv_storage[i++] = secbuf; }
            argv_storage[i++] = (char*)path;
            break;
        case HOSC_AUDIO_PLAYER_MPV:
            argv_storage[i++] = "mpv"; argv_storage[i++] = "--no-video"; argv_storage[i++] = "--really-quiet";
            if (start_seconds > 0.0) { snprintf(secbuf, 64, "--start=%.3f", start_seconds); argv_storage[i++] = secbuf; }
            argv_storage[i++] = (char*)path;
            break;
        case HOSC_AUDIO_PLAYER_CVLC:
            argv_storage[i++] = "cvlc"; argv_storage[i++] = "--play-and-exit"; argv_storage[i++] = "-q"; argv_storage[i++] = "--no-osd"; argv_storage[i++] = "-Idummy";
            if (start_seconds > 0.0) { snprintf(secbuf, 64, "--start-time=%.3f", start_seconds); argv_storage[i++] = secbuf; }
            argv_storage[i++] = (char*)path;
            break;
        case HOSC_AUDIO_PLAYER_MPG123:
            argv_storage[i++] = "mpg123"; argv_storage[i++] = "-q"; argv_storage[i++] = (char*)path;
            break;
        case HOSC_AUDIO_PLAYER_PAPLAY:
            argv_storage[i++] = "paplay"; argv_storage[i++] = (char*)path;
            break;
        case HOSC_AUDIO_PLAYER_APLAY:
            argv_storage[i++] = "aplay"; argv_storage[i++] = "-q"; argv_storage[i++] = (char*)path;
            break;
#ifdef __APPLE__
        case HOSC_AUDIO_PLAYER_AFPLAY:
            argv_storage[i++] = "afplay"; argv_storage[i++] = (char*)path;
            break;
#endif
        default:
            break;
    }
    argv_storage[i] = NULL;
}

static bool hosc_audio_spawn(HoscAudioPlayerKind kind, const char* path, double start_seconds) {
    char* argv_storage[16];
    char secbuf[64] = {0};
    pid_t pid;

    hosc_audio_build_argv(kind, path, start_seconds, argv_storage, secbuf);
    if (!argv_storage[0]) return false;

    if (!g_audio_sigchld_ignored) {
        /* Reap via explicit waitpid ourselves (position/duration queries
         * poll via WNOHANG), so just make sure SIGCHLD doesn't get an
         * inherited handler that misbehaves. Default disposition is fine. */
        g_audio_sigchld_ignored = true;
    }

    pid = fork();
    if (pid < 0) return false;

    if (pid == 0) {
        int devnull = open("/dev/null", O_WRONLY);
        if (devnull >= 0) {
            dup2(devnull, STDOUT_FILENO);
            dup2(devnull, STDERR_FILENO);
            if (devnull > STDERR_FILENO) close(devnull);
        }
        execvp(argv_storage[0], argv_storage);
        _exit(127);
    }

    g_audio_child_pid = pid;
    g_audio_playing = true;
    clock_gettime(CLOCK_MONOTONIC, &g_audio_start_ts);
    return true;
}

/* Runs `ffprobe` synchronously (quick, metadata-only) to fetch duration in
 * milliseconds. Returns -1 if ffprobe isn't available or parsing fails. */
static int hosc_audio_probe_duration_ms(const char* path) {
    int pipefd[2];
    pid_t pid;
    char buf[128] = {0};
    ssize_t n;
    double seconds;

    if (!hosc_audio_find_in_path("ffprobe")) return -1;
    if (pipe(pipefd) != 0) return -1;

    pid = fork();
    if (pid < 0) { close(pipefd[0]); close(pipefd[1]); return -1; }

    if (pid == 0) {
        char* argv_storage[] = {
            "ffprobe", "-v", "error", "-show_entries", "format=duration",
            "-of", "default=noprint_wrappers=1:nokey=1", (char*)path, NULL
        };
        int devnull = open("/dev/null", O_WRONLY);
        close(pipefd[0]);
        dup2(pipefd[1], STDOUT_FILENO);
        if (devnull >= 0) dup2(devnull, STDERR_FILENO);
        close(pipefd[1]);
        execvp(argv_storage[0], argv_storage);
        _exit(127);
    }

    close(pipefd[1]);
    n = read(pipefd[0], buf, sizeof(buf) - 1);
    close(pipefd[0]);
    waitpid(pid, NULL, 0);

    if (n <= 0) return -1;
    buf[n] = '\0';
    seconds = atof(buf);
    if (seconds <= 0.0) return -1;
    return (int)(seconds * 1000.0);
}
#endif /* HOSC_HAVE_LIBMPV / HOSC_HAVE_LIBVLC / exec fallback */
#endif /* !_WIN32 */

/* ==========================================================================
 * Dynamic backend functions (cross-platform dispatch)
 * ========================================================================== */

/* Forward declarations of built-in backends */
extern HVM_GuiBackend hvm_gui_backend_console;
extern HVM_GuiBackend hvm_gui_backend_win32;
extern HVM_GuiBackend hvm_gui_backend_x11;

/* Event callback: pushes events from backend into HOSC event queue */
static void hosc_gui_event_callback(int type, int key_code, int mouse_x, int mouse_y, int mouse_button, void* userdata) {
    (void)userdata;
    switch (type) {
        case 1: hosc_gui_push_event(HOSC_GUI_EVENT_QUIT, key_code, mouse_x, mouse_y, mouse_button); break;
        case 2: hosc_gui_push_event(HOSC_GUI_EVENT_KEY_DOWN, key_code, mouse_x, mouse_y, mouse_button); break;
        case 3: hosc_gui_push_event(HOSC_GUI_EVENT_KEY_UP, key_code, mouse_x, mouse_y, mouse_button); break;
        case 4: hosc_gui_push_event(HOSC_GUI_EVENT_MOUSE_MOVE, key_code, mouse_x, mouse_y, mouse_button); break;
        case 5: hosc_gui_push_event(HOSC_GUI_EVENT_MOUSE_DOWN, key_code, mouse_x, mouse_y, mouse_button); break;
        case 6: hosc_gui_push_event(HOSC_GUI_EVENT_MOUSE_UP, key_code, mouse_x, mouse_y, mouse_button); break;
    }
}

/* Update g_gui_backend enum based on selected backend name */
static void hosc_gui_update_backend_enum(void) {
    if (!g_active_backend) return;
    if (strcmp(g_active_backend->name, "win32") == 0)
        g_gui_backend = HOSC_GUI_BACKEND_WIN32;
    else if (strcmp(g_active_backend->name, "x11") == 0)
        g_gui_backend = HOSC_GUI_BACKEND_X11;
    else
        g_gui_backend = HOSC_GUI_BACKEND_CONSOLE;
}

bool hosc_gui_init(void) {
    if (g_gui_initialized) return true;
    g_gui_backend = HOSC_GUI_BACKEND_CONSOLE;
    g_active_backend = NULL;
    g_active_window = NULL;
    hosc_gui_clear_event_queue();

    /* Register backends - order determines priority.
     * Platform-specific backends must come first: console.available()
     * always returns 1, so if it were registered first it would win
     * before x11/win32 ever get a chance to be probed. */
#ifdef _WIN32
    hvm_gui_backend_register(&hvm_gui_backend_win32);
#endif
#if defined(__linux__)
    hvm_gui_backend_register(&hvm_gui_backend_x11);
#endif
    hvm_gui_backend_register(&hvm_gui_backend_console);

    /* Select best available backend at runtime */
    g_active_backend = hvm_gui_backend_select();
    hosc_gui_update_backend_enum();

#ifdef _WIN32
    if (g_gui_backend == HOSC_GUI_BACKEND_WIN32) {
        hosc_register_window_class();
    }
#endif

    g_gui_initialized = true;
    g_gui_running = false;
    return true;
}

void hosc_gui_shutdown(void) {
    if (g_active_backend && g_active_window) {
        g_active_backend->destroy_window(g_active_window);
        g_active_window = NULL;
    }
#ifdef _WIN32
    if (g_gui_window) { DestroyWindow(g_gui_window); g_gui_window = NULL; }
    hosc_release_backbuffer();
    hosc_media_foundation_shutdown();
    hosc_gdiplus_shutdown();
    hosc_destroy_window_icon();
    g_gui_min_width = 0; g_gui_min_height = 0;
#else
#if defined(HOSC_HAVE_LIBMPV)
    hosc_mpv_shutdown();
#elif defined(HOSC_HAVE_LIBVLC)
    hosc_vlc_shutdown();
#else
    hosc_audio_stop_internal();
#endif
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
        case HOSC_GUI_BACKEND_WIN32: return "win32";
        case HOSC_GUI_BACKEND_X11: return "x11";
        case HOSC_GUI_BACKEND_CONSOLE: default: return "console";
    }
}

bool hosc_gui_create_window(const char* title, int width, int height) {
    HOSCGUIWindowOptions options;
    memset(&options, 0, sizeof(options));
    options.title = title; options.width = width; options.height = height; options.resizable = true;
    return hosc_gui_create_window_ex(&options);
}

bool hosc_gui_create_window_ex(const HOSCGUIWindowOptions* options) {
    const char* title = "HOSC Window";
    int width = 800, height = 600, min_width = 0, min_height = 0;
    bool resizable = true, fullscreen = false, center = false;
    const char* icon = NULL;

    if (!g_gui_initialized) hosc_gui_init();

    if (options) {
        if (options->title) title = options->title;
        if (options->width > 0) width = options->width;
        if (options->height > 0) height = options->height;
        if (options->min_width > 0) min_width = options->min_width;
        if (options->min_height > 0) min_height = options->min_height;
        if (options->icon) icon = options->icon;
        resizable = options->resizable;
        fullscreen = options->fullscreen;
        center = options->center;
    }

#ifdef _WIN32
    /* Win32 native window creation (legacy path, kept for backbuffer/audio) */
    if (g_gui_backend == HOSC_GUI_BACKEND_WIN32) {
        DWORD style = WS_OVERLAPPEDWINDOW;
        int x = CW_USEDEFAULT, y = CW_USEDEFAULT, show = SW_SHOW;
        if (fullscreen) { style = WS_POPUP | WS_VISIBLE; width = GetSystemMetrics(SM_CXSCREEN); height = GetSystemMetrics(SM_CYSCREEN); x = y = 0; show = SW_MAXIMIZE; }
        else if (!resizable) style = WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX;
        if (g_gui_window) { DestroyWindow(g_gui_window); g_gui_window = NULL; }
        g_gui_min_width = min_width; g_gui_min_height = min_height;
        if (center && !fullscreen) {
            int sw = GetSystemMetrics(SM_CXSCREEN), sh = GetSystemMetrics(SM_CYSCREEN);
            x = (sw - width) / 2; y = (sh - height) / 2;
            if (x < 0) x = 0; if (y < 0) y = 0;
        }
        g_gui_window = CreateWindowExA(0, HOSC_WINDOW_CLASS_NAME, title, style, x, y, width, height, NULL, NULL, GetModuleHandleA(NULL), NULL);
        if (g_gui_window) {
            hosc_apply_window_icon(g_gui_window, icon);
            hosc_ensure_backbuffer(width, height);
            ShowWindow(g_gui_window, show); UpdateWindow(g_gui_window);
            g_gui_running = true; hosc_gui_clear_event_queue();
            return true;
        }
        hosc_destroy_window_icon();
        g_gui_backend = HOSC_GUI_BACKEND_CONSOLE;
    }
#endif

    /* Dynamic backend window creation (X11, etc.) */
    if (g_active_backend && g_active_backend->create_window) {
        g_active_window = g_active_backend->create_window(title, width, height, resizable, fullscreen,
                                                           icon, min_width, min_height, center,
                                                           hosc_gui_event_callback, NULL);
        if (g_active_window) {
            g_gui_running = true;
            hosc_gui_clear_event_queue();
            return true;
        }
    }

    /* Console fallback */
    printf("[GUI:console] create_window title=\"%s\" size=%dx%d resizable=%s fullscreen=%s center=%s min=%dx%d icon=\"%s\"\n",
           title, width, height, resizable?"true":"false", fullscreen?"true":"false", center?"true":"false", min_width, min_height, icon?icon:"");
    g_gui_running = true;
    hosc_gui_clear_event_queue();
    return true;
}

void hosc_gui_draw_text(int x, int y, const char* text) {
    if (!text) text = "";

#ifdef _WIN32
    /* Win32 native drawing (legacy path) */
    if (g_gui_backend == HOSC_GUI_BACKEND_WIN32 && g_gui_window) {
        HDC dc = hosc_gui_begin_draw();
        if (dc) { TextOutA(dc, x, y, text, (int)strlen(text)); hosc_gui_present(); }
        hosc_gui_pump_events();
        return;
    }
#endif

    /* Dynamic backend drawing.
     * Default text color must be readable against the window's white
     * background - (240,244,252) is near-white and was invisible. */
    if (g_active_backend && g_active_window && g_active_backend->draw_text) {
        g_active_backend->draw_text(g_active_window, x, y, text, 24, 26, 32, 16, 0);
        return;
    }

    printf("[GUI:console] text x=%d y=%d msg=\"%s\"\n", x, y, text);
}

void hosc_gui_draw_text_styled(int x, int y, const char* text, int size, int r, int g, int b, bool bold) {
    if (!text) text = "";
    if (size <= 0) size = 16;

#ifdef _WIN32
    if (g_gui_backend == HOSC_GUI_BACKEND_WIN32 && g_gui_window) {
        HDC dc = hosc_gui_begin_draw();
        if (dc) {
            HFONT font = CreateFontA(-size, 0, 0, 0, bold?FW_BOLD:FW_NORMAL, FALSE, FALSE, FALSE,
                                      DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                                      CLEARTYPE_QUALITY, DEFAULT_PITCH|FF_DONTCARE, "Segoe UI");
            HGDIOBJ of = SelectObject(dc, font);
            COLORREF oc = SetTextColor(dc, RGB(r,g,b));
            int ob = SetBkMode(dc, TRANSPARENT);
            TextOutA(dc, x, y, text, (int)strlen(text));
            SetBkMode(dc, ob); SetTextColor(dc, oc); SelectObject(dc, of); DeleteObject(font);
            hosc_gui_present();
        }
        hosc_gui_pump_events();
        return;
    }
#endif

    /* Dynamic backend */
    if (g_active_backend && g_active_window && g_active_backend->draw_text) {
        g_active_backend->draw_text(g_active_window, x, y, text, r, g, b, size, bold ? 1 : 0);
        return;
    }

    printf("[GUI:console] text x=%d y=%d size=%d rgb=(%d,%d,%d) bold=%s msg=\"%s\"\n",
           x, y, size, r, g, b, bold?"true":"false", text);
}

void hosc_gui_draw_rect(int x, int y, int width, int height, int r, int g, int b, bool filled) {
#ifdef _WIN32
    if (g_gui_backend == HOSC_GUI_BACKEND_WIN32 && g_gui_window) {
        HDC dc = hosc_gui_begin_draw();
        if (dc) {
            RECT rect = {x, y, x+width, y+height};
            HBRUSH brush = CreateSolidBrush(RGB(r,g,b));
            HPEN pen = CreatePen(PS_SOLID, 1, RGB(r,g,b));
            HGDIOBJ ob = SelectObject(dc, filled ? brush : GetStockObject(NULL_BRUSH));
            HGDIOBJ op = SelectObject(dc, pen);
            Rectangle(dc, rect.left, rect.top, rect.right, rect.bottom);
            SelectObject(dc, ob); SelectObject(dc, op); DeleteObject(brush); DeleteObject(pen);
            hosc_gui_present();
        }
        hosc_gui_pump_events();
        return;
    }
#endif

    if (g_active_backend && g_active_window) {
        if (filled && g_active_backend->draw_rect)
            g_active_backend->draw_rect(g_active_window, x, y, width, height, r, g, b);
        else if (g_active_backend->draw_rect_outline)
            g_active_backend->draw_rect_outline(g_active_window, x, y, width, height, r, g, b, 1);
        return;
    }

    printf("[GUI:console] rect x=%d y=%d size=%dx%d rgb=(%d,%d,%d) filled=%s\n",
           x, y, width, height, r, g, b, filled?"true":"false");
}

void hosc_gui_draw_round_rect(int x, int y, int width, int height, int radius,
                              int fill_r, int fill_g, int fill_b,
                              int border_r, int border_g, int border_b,
                              int border_width) {
#ifdef _WIN32
    if (g_gui_backend == HOSC_GUI_BACKEND_WIN32 && g_gui_window) {
        HDC dc = hosc_gui_begin_draw();
        if (dc) {
            HBRUSH brush = CreateSolidBrush(RGB(fill_r, fill_g, fill_b));
            HPEN pen = border_width > 0 ? CreatePen(PS_SOLID, border_width, RGB(border_r, border_g, border_b)) : (HPEN)GetStockObject(NULL_PEN);
            int ew = radius*2 > 0 ? radius*2 : 2, eh = radius*2 > 0 ? radius*2 : 2;
            HGDIOBJ ob = SelectObject(dc, brush), op = SelectObject(dc, pen);
            RoundRect(dc, x, y, x+width, y+height, ew, eh);
            SelectObject(dc, ob); SelectObject(dc, op); DeleteObject(brush);
            if (border_width > 0) DeleteObject(pen);
            hosc_gui_present();
        }
        hosc_gui_pump_events();
        return;
    }
#endif

    if (g_active_backend && g_active_window && g_active_backend->draw_round_rect) {
        g_active_backend->draw_round_rect(g_active_window, x, y, width, height, radius,
                                           fill_r, fill_g, fill_b, border_r, border_g, border_b, border_width);
        return;
    }

    printf("[GUI:console] round_rect x=%d y=%d size=%dx%d radius=%d fill=(%d,%d,%d) border=(%d,%d,%d) border_width=%d\n",
           x, y, width, height, radius, fill_r, fill_g, fill_b, border_r, border_g, border_b, border_width);
}

void hosc_gui_draw_image(int x, int y, int width, int height, const char* image_path) {
    if (!image_path) image_path = "";

#ifdef _WIN32
    if (g_gui_backend == HOSC_GUI_BACKEND_WIN32 && g_gui_window && image_path[0]) {
        char norm[1024]; WCHAR wp[1024]; GpImage* img = NULL; GpGraphics* gfx = NULL;
        if (width <= 0 || height <= 0) { printf("[GUI:win32] image skipped: width/height must be > 0\n"); return; }
        if (!hosc_gdiplus_init()) { printf("[GUI:win32] image skipped: GDI+ init failed\n"); return; }
        hosc_normalize_windows_path(image_path, norm, sizeof(norm));
        if (!hosc_utf8_to_wide(norm, wp, (int)(sizeof(wp)/sizeof(wp[0])))) return;
        if (GdipLoadImageFromFile(wp, &img) != Ok || !img) { printf("[GUI:win32] image skipped: cannot load \"%s\"\n", norm); return; }
        HDC dc = hosc_gui_begin_draw();
        if (dc && GdipCreateFromHDC(dc, &gfx) == Ok && gfx) {
            GdipDrawImageRectI(gfx, img, x, y, width, height);
            GdipDeleteGraphics(gfx);
        }
        hosc_gui_present();
        GdipDisposeImage(img);
        hosc_gui_pump_events();
        return;
    }
#endif

    /* Dynamic backend drawing (x11, etc.) */
    if (g_active_backend && g_active_window && g_active_backend->draw_image) {
        g_active_backend->draw_image(g_active_window, x, y, width, height, image_path);
        return;
    }

    printf("[GUI:console] image x=%d y=%d size=%dx%d path=\"%s\"\n", x, y, width, height, image_path);
}

bool hosc_audio_play_file(const char* audio_path, bool async_play) {
    if (!audio_path || !audio_path[0]) return false;
#ifdef _WIN32
    {
        char norm[1024]; WCHAR wp[1024]; HRESULT hr;
        hosc_audio_stop_internal();
        hosc_normalize_windows_path(audio_path, norm, sizeof(norm));
        if (!hosc_utf8_to_wide(norm, wp, (int)(sizeof(wp)/sizeof(wp[0])))) { fprintf(stderr, "[audio] invalid UTF-8 path: %s\n", audio_path); return false; }
        if (!hosc_media_foundation_init()) { fprintf(stderr, "[audio] Media Foundation init failed\n"); return false; }
        hr = MFPCreateMediaPlayer(wp, async_play?TRUE:FALSE, MFP_OPTION_FREE_THREADED_CALLBACK, &g_hosc_media_callback.iface, NULL, &g_audio_player);
        if (FAILED(hr) || !g_audio_player) { fprintf(stderr, "[audio] MFPlay open failed for \"%s\" (hr=0x%08lx)\n", norm, (unsigned long)hr); return false; }
        g_audio_internal_playback = true; g_audio_ready = true;
        strncpy(g_last_audio_path, norm, sizeof(g_last_audio_path)-1); g_last_audio_path[sizeof(g_last_audio_path)-1] = '\0';
        if (!async_play) g_audio_player->lpVtbl->Play(g_audio_player);
        return true;
    }
#elif defined(HOSC_HAVE_LIBMPV)
    {
        char resolved[1024];
        const char* cmd[4];
        (void)async_play;

        if (!hosc_mpv_ensure()) {
            fprintf(stderr, "[audio] failed to initialize libmpv\n");
            return false;
        }
        hosc_runtime_resolve_path(audio_path, resolved, sizeof(resolved));
        cmd[0] = "loadfile"; cmd[1] = resolved; cmd[2] = "replace"; cmd[3] = NULL;
        strncpy(g_last_audio_path, resolved, sizeof(g_last_audio_path) - 1);
        g_last_audio_path[sizeof(g_last_audio_path) - 1] = '\0';
        if (mpv_command(g_mpv, cmd) < 0) {
            fprintf(stderr, "[audio] libmpv failed to load \"%s\"\n", resolved);
            return false;
        }
        /* loadfile is itself async inside mpv; this catches anything that
         * surfaces immediately, further errors surface via pump_events(). */
        hosc_mpv_drain_log();
        return true;
    }
#elif defined(HOSC_HAVE_LIBVLC)
    {
        char resolved[1024];
        libvlc_media_t* media;
        (void)async_play;

        if (!hosc_vlc_ensure()) {
            fprintf(stderr, "[audio] failed to initialize libvlc\n");
            return false;
        }
        hosc_audio_stop_internal();
        hosc_runtime_resolve_path(audio_path, resolved, sizeof(resolved));

        media = libvlc_media_new_path(g_vlc, resolved);
        if (!media) {
            fprintf(stderr, "[audio] libvlc failed to open \"%s\"\n", resolved);
            return false;
        }
        g_vlc_player = libvlc_media_player_new_from_media(media);
        libvlc_media_release(media);
        if (!g_vlc_player) {
            fprintf(stderr, "[audio] libvlc failed to create a player for \"%s\"\n", resolved);
            return false;
        }
        if (libvlc_media_player_play(g_vlc_player) != 0) {
            fprintf(stderr, "[audio] libvlc failed to play \"%s\"\n", resolved);
            libvlc_media_player_release(g_vlc_player);
            g_vlc_player = NULL;
            return false;
        }
        strncpy(g_last_audio_path, resolved, sizeof(g_last_audio_path) - 1);
        g_last_audio_path[sizeof(g_last_audio_path) - 1] = '\0';
        return true;
    }
#else
    {
        HoscAudioPlayerKind kind = hosc_audio_detect_player();
        char resolved[1024];
        (void)async_play; /* framework always calls with async_play=true; POSIX backend is always async */

        hosc_audio_stop_internal();
        hosc_runtime_resolve_path(audio_path, resolved, sizeof(resolved));

        if (kind == HOSC_AUDIO_PLAYER_NONE) {
            fprintf(stderr,
                "[audio] no audio player found on this system for \"%s\".\n"
                "[audio] install one of: ffmpeg (ffplay), mpv, mpg123, vlc (cvlc), "
                "or PulseAudio/ALSA utils (paplay/aplay) - no HOSC code changes "
                "needed once one is on PATH.\n",
                resolved);
            return false;
        }

        if (!hosc_audio_spawn(kind, resolved, 0.0)) {
            fprintf(stderr, "[audio] failed to launch player for \"%s\"\n", resolved);
            return false;
        }

        g_audio_player_kind = kind;
        g_audio_seek_offset_ms = 0;
        g_audio_duration_ms_cache = hosc_audio_probe_duration_ms(resolved);
        strncpy(g_last_audio_path, resolved, sizeof(g_last_audio_path) - 1);
        g_last_audio_path[sizeof(g_last_audio_path) - 1] = '\0';
        return true;
    }
#endif
}

void hosc_audio_stop(void) {
    hosc_audio_stop_internal();
}

bool hosc_gui_pick_audio_file(char* out_path, size_t out_cap) {
#ifdef _WIN32
    OPENFILENAMEW d; WCHAR buf[MAX_PATH];
    if (!out_path || out_cap == 0) return false;
    memset(buf, 0, sizeof(buf)); memset(&d, 0, sizeof(d));
    d.lStructSize = sizeof(d); d.hwndOwner = g_gui_window; d.lpstrFile = buf; d.nMaxFile = MAX_PATH;
    d.lpstrFilter = L"Audio Files\0*.mp3;*.wav;*.ogg;*.flac;*.m4a\0All Files\0*.*\0";
    d.nFilterIndex = 1; d.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_HIDEREADONLY;
    d.lpstrTitle = L"Select audio file";
    if (!GetOpenFileNameW(&d)) return false;
    return hosc_wide_to_utf8(buf, out_path, (int)out_cap);
#else
    (void)out_path; (void)out_cap;
    return false;
#endif
}

bool hosc_audio_has_internal_playback(void) {
#ifdef _WIN32
    return (g_audio_internal_playback && g_audio_player != NULL);
#elif defined(HOSC_HAVE_LIBMPV)
    {
        int idle = 1;
        if (!g_mpv || !g_last_audio_path[0]) return false;
        if (mpv_get_property(g_mpv, "idle-active", MPV_FORMAT_FLAG, &idle) < 0) return false;
        return !idle;
    }
#elif defined(HOSC_HAVE_LIBVLC)
    return g_vlc_player != NULL && libvlc_media_player_is_playing(g_vlc_player);
#else
    hosc_audio_reap_if_finished();
    return g_audio_playing;
#endif
}

int hosc_audio_get_position_ms(void) {
#ifdef _WIN32
    PROPVARIANT v;
    if (!hosc_audio_has_internal_playback()) return -1;
    PropVariantInit(&v);
    if (FAILED(g_audio_player->lpVtbl->GetPosition(g_audio_player, &MFP_POSITIONTYPE_100NS, &v))) return -1;
    if (v.vt != VT_I8 && v.vt != VT_UI8) { PropVariantClear(&v); return -1; }
    long long r = (v.vt == VT_I8 ? v.hVal.QuadPart : (long long)v.uhVal.QuadPart);
    int ms = (int)(r / 10000LL); PropVariantClear(&v); return ms;
#elif defined(HOSC_HAVE_LIBMPV)
    {
        double pos = 0.0;
        if (!hosc_audio_has_internal_playback()) return -1;
        if (mpv_get_property(g_mpv, "time-pos", MPV_FORMAT_DOUBLE, &pos) < 0) return -1;
        return (int)(pos * 1000.0);
    }
#elif defined(HOSC_HAVE_LIBVLC)
    {
        libvlc_time_t t;
        if (!g_vlc_player) return -1;
        t = libvlc_media_player_get_time(g_vlc_player);
        return (t < 0) ? -1 : (int)t;
    }
#else
    struct timespec now;
    long elapsed_ms;

    if (!hosc_audio_has_internal_playback()) return -1;
    clock_gettime(CLOCK_MONOTONIC, &now);
    elapsed_ms = (long)((now.tv_sec - g_audio_start_ts.tv_sec) * 1000L +
                         (now.tv_nsec - g_audio_start_ts.tv_nsec) / 1000000L);
    return (int)(g_audio_seek_offset_ms + elapsed_ms);
#endif
}

int hosc_audio_get_duration_ms(void) {
#ifdef _WIN32
    PROPVARIANT v;
    if (!hosc_audio_has_internal_playback()) return -1;
    PropVariantInit(&v);
    if (FAILED(g_audio_player->lpVtbl->GetDuration(g_audio_player, &MFP_POSITIONTYPE_100NS, &v))) return -1;
    if (v.vt != VT_I8 && v.vt != VT_UI8) { PropVariantClear(&v); return -1; }
    long long r = (v.vt == VT_I8 ? v.hVal.QuadPart : (long long)v.uhVal.QuadPart);
    int ms = (int)(r / 10000LL); PropVariantClear(&v); return ms;
#elif defined(HOSC_HAVE_LIBMPV)
    {
        double dur = 0.0;
        if (!hosc_audio_has_internal_playback()) return -1;
        if (mpv_get_property(g_mpv, "duration", MPV_FORMAT_DOUBLE, &dur) < 0) return -1;
        return (int)(dur * 1000.0);
    }
#elif defined(HOSC_HAVE_LIBVLC)
    {
        libvlc_time_t d;
        if (!g_vlc_player) return -1;
        d = libvlc_media_player_get_length(g_vlc_player);
        return (d < 0) ? -1 : (int)d;
    }
#else
    if (!hosc_audio_has_internal_playback()) return -1;
    return g_audio_duration_ms_cache;
#endif
}

bool hosc_audio_seek_ms(int position_ms) {
#ifdef _WIN32
    PROPVARIANT v; int dur;
    if (!hosc_audio_has_internal_playback()) return false;
    dur = hosc_audio_get_duration_ms();
    if (dur > 0) { if (position_ms < 0) position_ms = 0; if (position_ms > dur) position_ms = dur; }
    PropVariantInit(&v); v.vt = VT_I8; v.hVal.QuadPart = (LONGLONG)position_ms * 10000LL;
    if (FAILED(g_audio_player->lpVtbl->SetPosition(g_audio_player, &MFP_POSITIONTYPE_100NS, &v))) return false;
    if (FAILED(g_audio_player->lpVtbl->Play(g_audio_player))) return false;
    return true;
#elif defined(HOSC_HAVE_LIBMPV)
    {
        char secbuf[64];
        const char* cmd[] = {"seek", secbuf, "absolute", NULL};
        if (!hosc_audio_has_internal_playback()) return false;
        if (position_ms < 0) position_ms = 0;
        snprintf(secbuf, sizeof(secbuf), "%.3f", position_ms / 1000.0);
        return mpv_command(g_mpv, cmd) >= 0;
    }
#elif defined(HOSC_HAVE_LIBVLC)
    {
        if (!g_vlc_player) return false;
        if (position_ms < 0) position_ms = 0;
        libvlc_media_player_set_time(g_vlc_player, (libvlc_time_t)position_ms);
        return true;
    }
#else
    {
        int dur;
        double start_seconds;
        HoscAudioPlayerKind kind = g_audio_player_kind;
        char path[1024];

        if (!hosc_audio_has_internal_playback() || kind == HOSC_AUDIO_PLAYER_NONE) return false;
        dur = g_audio_duration_ms_cache;
        if (dur > 0) { if (position_ms < 0) position_ms = 0; if (position_ms > dur) position_ms = dur; }
        if (position_ms < 0) position_ms = 0;

        strncpy(path, g_last_audio_path, sizeof(path) - 1);
        path[sizeof(path) - 1] = '\0';

        hosc_audio_stop_internal();
        start_seconds = position_ms / 1000.0;
        if (!hosc_audio_spawn(kind, path, start_seconds)) return false;
        g_audio_player_kind = kind;
        g_audio_seek_offset_ms = position_ms;
        return true;
    }
#endif
}

void hosc_gui_pump_events(void) {
#ifdef _WIN32
    MSG msg;
    while (PeekMessageA(&msg, NULL, 0, 0, PM_REMOVE)) { TranslateMessage(&msg); DispatchMessageA(&msg); }
#endif
#if !defined(_WIN32) && defined(HOSC_HAVE_LIBMPV)
    hosc_mpv_drain_log();
#endif
    /* X11 events are pumped via backend pump_events - called explicitly */
    if (g_active_backend && g_active_window && g_active_backend->pump_events) {
        g_active_backend->pump_events(g_active_window);
    }
}

bool hosc_gui_poll_event(HOSCGUIEvent* out_event) {
    HOSCGUIEvent empty_event;
    hosc_gui_pump_events();
    if (hosc_gui_pop_event(out_event)) return true;
    if (out_event) {
        empty_event.type = HOSC_GUI_EVENT_NONE;
        empty_event.key_code = 0; empty_event.mouse_x = 0; empty_event.mouse_y = 0; empty_event.mouse_button = 0;
        *out_event = empty_event;
    }
    return false;
}

bool hosc_gui_is_running(void) {
    if (g_active_backend && g_active_window && g_active_backend->is_running) {
        return g_active_backend->is_running(g_active_window);
    }
    return g_gui_running;
}

void hosc_gui_suspend_present(void) {
#ifdef _WIN32
    if (g_gui_backend == HOSC_GUI_BACKEND_WIN32) g_gui_present_suspend_count++;
#endif
}

void hosc_gui_resume_present(void) {
#ifdef _WIN32
    if (g_gui_backend == HOSC_GUI_BACKEND_WIN32 && g_gui_present_suspend_count > 0) g_gui_present_suspend_count--;
#endif
}

void hosc_gui_flush(void) {
#ifdef _WIN32
    if (g_gui_backend == HOSC_GUI_BACKEND_WIN32) hosc_gui_present();
#endif
}

void hosc_runtime_set_base_dir(const char* base_dir) {
    if (!base_dir || !base_dir[0]) { g_runtime_base_dir[0] = '\0'; return; }
    strncpy(g_runtime_base_dir, base_dir, sizeof(g_runtime_base_dir) - 1);
    g_runtime_base_dir[sizeof(g_runtime_base_dir) - 1] = '\0';
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
    if (!module) return false;
    entry = g_module_list;
    while (entry) { if (strcmp(entry->module->name, module->name) == 0) return true; entry = entry->next; }
    entry = (ModuleEntry*)memory_allocate(sizeof(ModuleEntry));
    if (!entry) return false;
    entry->module = module; entry->next = g_module_list; g_module_list = entry;
    if (module->init) module->init();
    return true;
}

static HOSCModule* module_get(const char* module_name) {
    ModuleEntry* entry = g_module_list;
    while (entry) { if (strcmp(entry->module->name, module_name) == 0) return entry->module; entry = entry->next; }
    return NULL;
}

static HOSCModule* module_load(const char* module_name) {
    HOSCModule* module = NULL;
    if (!module_name) return NULL;
    module = module_get(module_name);
    if (module) return module;
    if (strcmp(module_name, "core") == 0) module = &hosc_core_module;
    else if (strcmp(module_name, "io") == 0) module = &hosc_io_module;
    else if (strcmp(module_name, "math") == 0) module = &hosc_math_module;
    else if (strcmp(module_name, "string") == 0) module = &hosc_string_module;
    else if (strcmp(module_name, "win32") == 0) module = &hosc_win32_module;
    else if (strcmp(module_name, "gui") == 0) module = &hosc_gui_module;
    if (!module) return NULL;
    if (!module_registry_add(module)) return NULL;
    return module;
}

static void module_unload(HOSCModule* module) {
    ModuleEntry** current = &g_module_list;
    while (*current) {
        if ((*current)->module == module) {
            ModuleEntry* to_remove = *current;
            *current = (*current)->next;
            if (to_remove->module && to_remove->module->cleanup) to_remove->module->cleanup(NULL);
            memory_deallocate(to_remove);
            return;
        }
        current = &(*current)->next;
    }
}

static void module_list_all(void) {
    ModuleEntry* entry = g_module_list;
    printf("=== Loaded Modules ===\n");
    while (entry) { printf("- %s v%s\n", entry->module->name, entry->module->version); entry = entry->next; }
    printf("======================\n");
}

static HOSCModuleRegistry g_module_registry = {
    .load_module = module_load, .unload_module = module_unload,
    .get_module = module_get, .list_modules = module_list_all
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
    HOSCAPIFunction* func; APIFunctionEntry* entry;
    func = (HOSCAPIFunction*)memory_allocate(sizeof(HOSCAPIFunction));
    if (!func) return NULL;
    func->name = hosc_strdup_local(name ? name : "");
    func->signature = hosc_strdup_local(signature ? signature : "");
    func->implementation = implementation;
    func->validate_args = NULL;
    entry = (APIFunctionEntry*)memory_allocate(sizeof(APIFunctionEntry));
    if (!entry) { memory_deallocate((void*)func->name); memory_deallocate((void*)func->signature); memory_deallocate(func); return NULL; }
    entry->function = func; entry->next = g_api_list; g_api_list = entry;
    return func;
}

static HOSCAPIFunction* api_get_function(const char* name) {
    APIFunctionEntry* entry = g_api_list;
    while (entry) { if (strcmp(entry->function->name, name) == 0) return entry->function; entry = entry->next; }
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
    while (entry) { printf("- %s: %s\n", entry->function->name, entry->function->signature); entry = entry->next; }
    printf("===============================\n");
}

static HOSCAPIRegistry g_api_registry = {
    .register_function = api_register_function, .get_function = api_get_function,
    .unregister_function = api_unregister_function, .list_functions = api_list_functions
};

// ============================================================================
// ERROR HANDLING
// ============================================================================

static HOSCError* g_last_error = NULL;
static void (*g_error_handler)(HOSCError*) = NULL;

static void error_report(HOSCError* error) {
    if (g_last_error) memory_deallocate(g_last_error);
    g_last_error = error;
    if (g_error_handler) g_error_handler(error);
    else { printf("HOSC Error [%d]: %s\n", error->code, error->message ? error->message : ""); if (error->file) printf("  File: %s:%d\n", error->file, error->line); }
}

static void error_clear(void) { if (g_last_error) { memory_deallocate(g_last_error); g_last_error = NULL; } }
static HOSCError* error_get_last(void) { return g_last_error; }
static void error_set_handler(void (*handler)(HOSCError*)) { g_error_handler = handler; }

static HOSCErrorHandler g_error_handler_impl = {
    .report_error = error_report, .clear_errors = error_clear,
    .get_last_error = error_get_last, .set_error_handler = error_set_handler
};

// ============================================================================
// LOGGER
// ============================================================================

static HOSCLogLevel g_log_level = HOSC_LOG_INFO;
static FILE* g_log_file = NULL;

static const char* log_level_to_string(HOSCLogLevel level) {
    switch (level) {
        case HOSC_LOG_DEBUG: return "DEBUG";
        case HOSC_LOG_INFO: return "INFO";
        case HOSC_LOG_WARNING: return "WARNING";
        case HOSC_LOG_ERROR: return "ERROR";
        case HOSC_LOG_FATAL: return "FATAL";
        default: return "UNKNOWN";
    }
}

static void logger_log(HOSCLogLevel level, const char* message, ...) {
    FILE* output; time_t now; struct tm* tm_info; char timestamp[64]; char buf[1024];
    va_list args;
    if (level < g_log_level) return;
    now = time(NULL); tm_info = localtime(&now);
    if (tm_info) strftime(timestamp, sizeof(timestamp), "%Y-%m-%d %H:%M:%S", tm_info);
    else strcpy(timestamp, "0000-00-00 00:00:00");
    va_start(args, message); vsnprintf(buf, sizeof(buf), message, args); va_end(args);
    output = g_log_file ? g_log_file : stdout;
    fprintf(output, "[%s] [%s] %s\n", timestamp, log_level_to_string(level), buf);
    if (g_log_file) fflush(g_log_file);
}

static void logger_set_level(HOSCLogLevel level) { g_log_level = level; }

static void logger_set_output(const char* file) {
    if (g_log_file && g_log_file != stdout) fclose(g_log_file);
    g_log_file = file ? fopen(file, "a") : stdout;
    if (!g_log_file) g_log_file = stdout;
}

static void logger_flush(void) { if (g_log_file) fflush(g_log_file); }

static HOSCLogger g_logger_impl = {
    .log = logger_log, .set_level = logger_set_level,
    .set_output = logger_set_output, .flush = logger_flush
};

// ============================================================================
// STANDARD LIBRARY
// ============================================================================

HOSCString* hosc_string_create(const char* data) {
    HOSCString* str; if (!data) data = "";
    str = (HOSCString*)memory_allocate(sizeof(HOSCString));
    if (!str) return NULL;
    str->length = strlen(data);
    str->data = (char*)memory_allocate(str->length + 1);
    if (!str->data) { memory_deallocate(str); return NULL; }
    memcpy(str->data, data, str->length + 1);
    return str;
}

void hosc_string_destroy(HOSCString* str) {
    if (!str) return;
    if (str->data) memory_deallocate(str->data);
    memory_deallocate(str);
}

HOSCArray* hosc_array_create(size_t initial_capacity) {
    HOSCArray* array = (HOSCArray*)memory_allocate(sizeof(HOSCArray));
    if (!array) return NULL;
    array->capacity = initial_capacity > 0 ? initial_capacity : 8;
    array->count = 0;
    array->items = (void**)memory_allocate(sizeof(void*) * array->capacity);
    if (!array->items) { memory_deallocate(array); return NULL; }
    return array;
}

void hosc_array_destroy(HOSCArray* array) {
    if (!array) return;
    if (array->items) memory_deallocate(array->items);
    memory_deallocate(array);
}

HOSCDictionary* hosc_dictionary_create(size_t initial_capacity) {
    HOSCDictionary* dict = (HOSCDictionary*)memory_allocate(sizeof(HOSCDictionary));
    if (!dict) return NULL;
    dict->capacity = initial_capacity > 0 ? initial_capacity : 8;
    dict->count = 0;
    dict->keys = (void**)memory_allocate(sizeof(void*) * dict->capacity);
    dict->values = (void**)memory_allocate(sizeof(void*) * dict->capacity);
    if (!dict->keys || !dict->values) {
        if (dict->keys) memory_deallocate(dict->keys);
        if (dict->values) memory_deallocate(dict->values);
        memory_deallocate(dict); return NULL;
    }
    return dict;
}

void hosc_dictionary_destroy(HOSCDictionary* dict) {
    if (!dict) return;
    if (dict->keys) memory_deallocate(dict->keys);
    if (dict->values) memory_deallocate(dict->values);
    memory_deallocate(dict);
}

// ============================================================================
// RUNTIME CORE
// ============================================================================

HOSCRuntimeContext* hosc_runtime_init(const HOSCRuntimeConfig* config) {
    if (g_runtime_context) return g_runtime_context;
    g_runtime_context = (HOSCRuntimeContext*)memory_allocate(sizeof(HOSCRuntimeContext));
    if (!g_runtime_context) return NULL;
    memset(g_runtime_context, 0, sizeof(HOSCRuntimeContext));
    g_runtime_context->state = HOSC_RUNTIME_STATE_INITIALIZED;
    g_runtime_context->runtime_id = ++g_runtime_counter;
    g_runtime_context->start_time = hosc_now_ms();
    if (config) g_runtime_context->config = *config;
    else {
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
    ModuleEntry* module_entry; APIFunctionEntry* api_entry;
    if (!context || context != g_runtime_context) return;
    hosc_log(HOSC_LOG_INFO, "Shutting down HOSC Runtime");
    context->state = HOSC_RUNTIME_STATE_SHUTDOWN;
    while (g_module_list) {
        module_entry = g_module_list; g_module_list = g_module_list->next;
        if (module_entry->module && module_entry->module->cleanup) module_entry->module->cleanup(NULL);
        memory_deallocate(module_entry);
    }
    while (g_api_list) {
        api_entry = g_api_list; g_api_list = g_api_list->next;
        memory_deallocate((void*)api_entry->function->name);
        memory_deallocate((void*)api_entry->function->signature);
        memory_deallocate(api_entry->function);
        memory_deallocate(api_entry);
    }
    if (g_last_error) { memory_deallocate(g_last_error); g_last_error = NULL; }
    hosc_gui_shutdown();
    if (g_log_file && g_log_file != stdout) fclose(g_log_file);
    g_log_file = NULL;
    memory_deallocate(g_runtime_context); g_runtime_context = NULL;
    if (g_memory_stats.tracking_enabled) memory_dump_stats();
}

HOSCRuntimeState hosc_runtime_get_state(HOSCRuntimeContext* context) {
    if (!context) return HOSC_RUNTIME_STATE_ERROR;
    return context->state;
}

HOSCMemoryManager* hosc_runtime_get_memory_manager(HOSCRuntimeContext* context) {
    return context ? (HOSCMemoryManager*)context->memory_manager : NULL;
}

void* hosc_allocate(size_t size) { return memory_allocate(size); }
void* hosc_reallocate(void* ptr, size_t new_size) { return memory_reallocate(ptr, new_size); }
void hosc_deallocate(void* ptr) { memory_deallocate(ptr); }

HOSCModuleRegistry* hosc_runtime_get_module_registry(HOSCRuntimeContext* context) {
    return context ? (HOSCModuleRegistry*)context->module_registry : NULL;
}

HOSCModule* hosc_load_module(const char* module_name) { return module_load(module_name); }
void hosc_unload_module(HOSCModule* module) { module_unload(module); }

HOSCAPIRegistry* hosc_runtime_get_api_registry(HOSCRuntimeContext* context) {
    return context ? (HOSCAPIRegistry*)context->api_registry : NULL;
}

void* hosc_call_function(const char* function_name, void* args) {
    HOSCAPIFunction* func = api_get_function(function_name);
    if (func && func->implementation) return func->implementation(args);
    return NULL;
}

HOSCErrorHandler* hosc_runtime_get_error_handler(HOSCRuntimeContext* context) {
    return context ? (HOSCErrorHandler*)context->error_handler : NULL;
}

void hosc_report_error(HOSCErrorType type, int code, const char* message, const char* file, int line) {
    HOSCError* error = (HOSCError*)memory_allocate(sizeof(HOSCError));
    if (!error) return;
    error->type = type; error->code = code;
    error->message = message ? message : "unknown error";
    error->file = file; error->line = line; error->timestamp = hosc_now_ms();
    error_report(error);
}

HOSCLogger* hosc_runtime_get_logger(HOSCRuntimeContext* context) {
    return context ? (HOSCLogger*)context->logger : NULL;
}

void hosc_log(HOSCLogLevel level, const char* message, ...) {
    va_list args; char buffer[1024];
    va_start(args, message);
    vsnprintf(buffer, sizeof(buffer), message, args);
    va_end(args);
    logger_log(level, buffer);
}