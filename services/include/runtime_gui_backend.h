/* runtime_gui_backend.h - Abstract GUI backend interface for HOSC runtime */
#ifndef RUNTIME_GUI_BACKEND_H
#define RUNTIME_GUI_BACKEND_H

#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Abstract window handle - defined per backend */
typedef struct HVM_GuiBackendWindow HVM_GuiBackendWindow;

/* Callback for event processing */
typedef void (*HVM_GuiEventCallback)(int type, int key_code, int mouse_x, int mouse_y, int mouse_button, void* userdata);

/* Backend interface */
typedef struct {
    /* Name of this backend (e.g. "win32", "x11", "console") */
    const char* name;
    
    /* Check if this backend is available on the current system */
    int (*available)(void);
    
    /* Initialize the backend */
    int (*init)(void);
    
    /* Shutdown the backend */
    void (*shutdown)(void);
    
    /* Create a window */
    HVM_GuiBackendWindow* (*create_window)(const char* title, int width, int height, 
                                            int resizable, int fullscreen, const char* icon,
                                            int min_width, int min_height, int center,
                                            HVM_GuiEventCallback callback, void* userdata);
    
    /* Destroy a window */
    void (*destroy_window)(HVM_GuiBackendWindow* window);
    
    /* Process pending events (non-blocking) */
    void (*pump_events)(HVM_GuiBackendWindow* window);
    
    /* Request repaint */
    void (*request_repaint)(HVM_GuiBackendWindow* window);
    
    /* Get tick count in milliseconds */
    double (*now_ms)(void);
    
    /* Sleep for given milliseconds */
    void (*sleep_ms)(int ms);
    
    /* Draw text */
    void (*draw_text)(HVM_GuiBackendWindow* window, int x, int y, const char* text, 
                      int r, int g, int b, int size, int bold);
    
    /* Draw an image loaded from a file (PNG/JPEG/etc.), scaled to w x h.
     * Optional - backends that can't support this may leave it NULL. */
    void (*draw_image)(HVM_GuiBackendWindow* window, int x, int y, int w, int h,
                       const char* image_path);
    
    /* Draw filled rectangle */
    void (*draw_rect)(HVM_GuiBackendWindow* window, int x, int y, int w, int h,
                      int r, int g, int b);
    
    /* Draw rectangle outline */
    void (*draw_rect_outline)(HVM_GuiBackendWindow* window, int x, int y, int w, int h,
                              int r, int g, int b, int border_width);
    
    /* Draw rounded rectangle */
    void (*draw_round_rect)(HVM_GuiBackendWindow* window, int x, int y, int w, int h, int radius,
                            int fill_r, int fill_g, int fill_b,
                            int border_r, int border_g, int border_b, int border_width);
    
    /* Get window width */
    int (*get_width)(HVM_GuiBackendWindow* window);
    
    /* Get window height */
    int (*get_height)(HVM_GuiBackendWindow* window);
    
    /* Get scroll position */
    int (*get_scroll_y)(HVM_GuiBackendWindow* window);
    
    /* Set scroll range */
    void (*set_scroll_range)(HVM_GuiBackendWindow* window, int range);
    
    /* Check if window is still running */
    int (*is_running)(HVM_GuiBackendWindow* window);
} HVM_GuiBackend;

/* Backend registration and selection */
int hvm_gui_backend_register(HVM_GuiBackend* backend);
const HVM_GuiBackend* hvm_gui_backend_select(void);
const HVM_GuiBackend* hvm_gui_backend_get(void);

/* Built-in backends */
extern HVM_GuiBackend hvm_gui_backend_console;
extern HVM_GuiBackend hvm_gui_backend_win32;
extern HVM_GuiBackend hvm_gui_backend_x11;

/* Resolves a possibly-relative asset path (icon, image, audio, ...) against
 * the running script's base directory, the same way the runtime already
 * does for audio on Windows. Implemented in hosc_runtime.c (linked into the
 * same binary) so every backend resolves paths the same way instead of each
 * one hardcoding its own assumption about the current working directory.
 * Always writes a NUL-terminated result to `output`, even on failure (falls
 * back to a copy of `input`). Returns false only if the arguments are invalid. */
bool hosc_runtime_resolve_path(const char* input, char* output, size_t output_cap);

#ifdef __cplusplus
}
#endif

#endif /* RUNTIME_GUI_BACKEND_H */