/* runtime_gui_backend_x11.c - X11 GUI backend for HOSC runtime */
#ifndef _POSIX_C_SOURCE
#define _POSIX_C_SOURCE 200809L
#endif
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <stdint.h>
#include <unistd.h>

#include "runtime_gui_backend.h"

#define STB_IMAGE_IMPLEMENTATION
#include "vendor/stb_image.h"

#ifdef __linux__
#define HAS_X11 1
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/Xos.h>
#include <X11/Xatom.h>
#include <X11/keysym.h>
#include <X11/Xft/Xft.h>
#else
#define HAS_X11 0
#endif

/* Event type constants matching HOSC_GUI_EVENT_* from hosc_runtime.h */
#define X11_EVENT_NONE 0
#define X11_EVENT_QUIT 1
#define X11_EVENT_KEY_DOWN 2
#define X11_EVENT_KEY_UP 3
#define X11_EVENT_MOUSE_MOVE 4
#define X11_EVENT_MOUSE_DOWN 5
#define X11_EVENT_MOUSE_UP 6

/* Maximum event queue size */
#define X11_EVENT_QUEUE_CAP 256

/* Forward declaration */
struct HVM_GuiBackendWindow {
#if HAS_X11
    Display* display;
    Window window;
    GC gc;
    int screen;
    int width;
    int height;
    int running;
    int scroll_y;
    int scroll_range;
#endif
    /* Event callback */
    HVM_GuiEventCallback callback;
    void* userdata;
    /* Event queue for compatibility */
    struct {
        int type;
        int key_code;
        int mouse_x;
        int mouse_y;
        int mouse_button;
    } event_queue[X11_EVENT_QUEUE_CAP];
    int queue_head;
    int queue_tail;
};

#if HAS_X11
static Display* g_x11_display = NULL;
static int g_x11_initialized = 0;

static int x11_push_event(struct HVM_GuiBackendWindow* win, int type, int key, int x, int y, int btn) {
    int next_tail = (win->queue_tail + 1) % X11_EVENT_QUEUE_CAP;
    if (next_tail == win->queue_head) {
        win->queue_head = (win->queue_head + 1) % X11_EVENT_QUEUE_CAP;
    }
    win->event_queue[win->queue_tail].type = type;
    win->event_queue[win->queue_tail].key_code = key;
    win->event_queue[win->queue_tail].mouse_x = x;
    win->event_queue[win->queue_tail].mouse_y = y;
    win->event_queue[win->queue_tail].mouse_button = btn;
    win->queue_tail = next_tail;
    return 1;
}

static int x11_pop_event(struct HVM_GuiBackendWindow* win) {
    if (win->queue_head == win->queue_tail) return 0;
    if (win->callback) {
        int idx = win->queue_head;
        win->callback(win->event_queue[idx].type, win->event_queue[idx].key_code,
                      win->event_queue[idx].mouse_x, win->event_queue[idx].mouse_y,
                      win->event_queue[idx].mouse_button, win->userdata);
    }
    win->queue_head = (win->queue_head + 1) % X11_EVENT_QUEUE_CAP;
    return 1;
}

static int x11_keycode_to_keysym(KeyCode keycode) {
    KeySym ks;
    {
        XKeyEvent ev;
        memset(&ev, 0, sizeof(ev));
        ev.display = g_x11_display;
        ev.keycode = keycode;
        ev.type = KeyPress;
        ks = XLookupKeysym(&ev, 0);
    }
    switch (ks) {
        case XK_Left: return 0x01000001;
        case XK_Right: return 0x01000002;
        case XK_Up: return 0x01000003;
        case XK_Down: return 0x01000004;
        case XK_Return: return 13;
        case XK_Escape: return 27;
        case XK_BackSpace: return 8;
        case XK_Tab: return 9;
        case XK_Shift_L: case XK_Shift_R: return 0x01000010;
        case XK_Control_L: case XK_Control_R: return 0x01000011;
        case XK_Alt_L: case XK_Alt_R: return 0x01000012;
        default: return (int)ks;
    }
}
#endif /* HAS_X11 */

/* Availability check - creates a temporary connection */
static int x11_available(void) {
#if HAS_X11
    const char* display_name = getenv("DISPLAY");
    if (!display_name || display_name[0] == '\0') return 0;
    
    Display* dpy = XOpenDisplay(NULL);
    if (dpy) {
        XCloseDisplay(dpy);
        return 1;
    }
    return 0;
#else
    return 0;
#endif
}

static int x11_init(void) {
#if HAS_X11
    if (g_x11_initialized) return 1;
    g_x11_display = XOpenDisplay(NULL);
    if (!g_x11_display) return 0;
    g_x11_initialized = 1;
    return 1;
#else
    return 0;
#endif
}

static void x11_shutdown(void) {
#if HAS_X11
    if (g_x11_display) {
        XCloseDisplay(g_x11_display);
        g_x11_display = NULL;
    }
    g_x11_initialized = 0;
#endif
}

static HVM_GuiBackendWindow* x11_create_window(const char* title, int width, int height,
                                                 int resizable, int fullscreen, const char* icon,
                                                 int min_width, int min_height, int center,
                                                 HVM_GuiEventCallback callback, void* userdata) {
#if HAS_X11
    struct HVM_GuiBackendWindow* win;
    XSizeHints* hints;
    XClassHint class_hint;
    Atom wm_delete;
    long event_mask;
    
    (void)icon;
    
    if (!g_x11_initialized && !x11_init()) return NULL;
    
    win = (struct HVM_GuiBackendWindow*)calloc(1, sizeof(struct HVM_GuiBackendWindow));
    if (!win) return NULL;
    
    win->display = g_x11_display;
    win->screen = DefaultScreen(win->display);
    win->width = width > 0 ? width : 800;
    win->height = height > 0 ? height : 600;
    win->running = 1;
    win->callback = callback;
    win->userdata = userdata;
    win->scroll_y = 0;
    win->scroll_range = 0;
    win->queue_head = 0;
    win->queue_tail = 0;
    
    /* Create window with a backing store so content drawn via draw_text/
     * draw_rect/etc. is retained and automatically replayed by the X
     * server on Expose, instead of being lost the first time the window
     * is covered, resized, or simply not yet viewable when drawn to. */
    {
        XSetWindowAttributes attrs;
        unsigned long attr_mask;
        memset(&attrs, 0, sizeof(attrs));
        attrs.background_pixel = WhitePixel(win->display, win->screen);
        attrs.border_pixel = BlackPixel(win->display, win->screen);
        attrs.backing_store = Always;
        attr_mask = CWBackPixel | CWBorderPixel | CWBackingStore;
        win->window = XCreateWindow(win->display, RootWindow(win->display, win->screen),
                                     0, 0, win->width, win->height, 1,
                                     DefaultDepth(win->display, win->screen), InputOutput,
                                     DefaultVisual(win->display, win->screen), attr_mask, &attrs);
    }
    
    /* Set title */
    XStoreName(win->display, win->window, title ? title : "HOSC Window");
    
    /* Set WM_CLASS */
    class_hint.res_name = (char*)"hosc_framework";
    class_hint.res_class = (char*)"HOSC";
    XSetClassHint(win->display, win->window, &class_hint);
    
    /* Set minimum size hints */
    hints = XAllocSizeHints();
    if (hints) {
        if (min_width > 0 && min_height > 0) {
            hints->flags = PMinSize;
            hints->min_width = min_width;
            hints->min_height = min_height;
        }
        if (!resizable) {
            hints->flags |= PMaxSize;
            hints->max_width = win->width;
            hints->max_height = win->height;
        }
        XSetWMNormalHints(win->display, win->window, hints);
        XFree(hints);
    }
    
    /* Center window if requested */
    if (center) {
        XWindowAttributes wa;
        XGetWindowAttributes(win->display, win->window, &wa);
        int screen_w = DisplayWidth(win->display, win->screen);
        int screen_h = DisplayHeight(win->display, win->screen);
        int x_pos = (screen_w - win->width) / 2;
        int y_pos = (screen_h - win->height) / 2;
        XMoveWindow(win->display, win->window, x_pos, y_pos);
    }
    
    /* Select events (must happen before mapping so we don't miss MapNotify) */
    event_mask = ExposureMask | StructureNotifyMask | KeyPressMask | KeyReleaseMask |
                 ButtonPressMask | ButtonReleaseMask | PointerMotionMask |
                 EnterWindowMask | LeaveWindowMask;
    XSelectInput(win->display, win->window, event_mask);
    
    /* Create GC */
    win->gc = XCreateGC(win->display, win->window, 0, NULL);
    
    /* Handle WM_DELETE_WINDOW */
    wm_delete = XInternAtom(win->display, "WM_DELETE_WINDOW", False);
    XSetWMProtocols(win->display, win->window, &wm_delete, 1);
    
    /* Show window, then block until the window manager actually maps it.
     * Without this, code immediately following create_window() (drawing
     * text/rects, etc.) races the window manager and draws to a window
     * that isn't viewable yet, which is silently lost. */
    XMapWindow(win->display, win->window);
    {
        XEvent map_ev;
        do {
            XWindowEvent(win->display, win->window, StructureNotifyMask, &map_ev);
        } while (map_ev.type != MapNotify);
    }
    
    /* Handle fullscreen - must be requested after mapping per EWMH spec */
    if (fullscreen) {
        Atom wm_state = XInternAtom(win->display, "_NET_WM_STATE", False);
        Atom fullscreen_atom = XInternAtom(win->display, "_NET_WM_STATE_FULLSCREEN", False);
        if (wm_state != None && fullscreen_atom != None) {
            XEvent xev;
            memset(&xev, 0, sizeof(xev));
            xev.type = ClientMessage;
            xev.xclient.window = win->window;
            xev.xclient.message_type = wm_state;
            xev.xclient.format = 32;
            xev.xclient.data.l[0] = 1; /* _NET_WM_STATE_ADD */
            xev.xclient.data.l[1] = (long)fullscreen_atom;
            xev.xclient.data.l[2] = 0;
            XSendEvent(win->display, DefaultRootWindow(win->display), False,
                       SubstructureNotifyMask, &xev);
        }
    }
    
    XFlush(win->display);
    
    return (HVM_GuiBackendWindow*)win;
#else
    (void)title; (void)width; (void)height; (void)resizable; (void)fullscreen;
    (void)icon; (void)min_width; (void)min_height; (void)center;
    (void)callback; (void)userdata;
    return NULL;
#endif
}

static void x11_destroy_window(HVM_GuiBackendWindow* w) {
#if HAS_X11
    struct HVM_GuiBackendWindow* win = (struct HVM_GuiBackendWindow*)w;
    if (!win) return;
    if (win->gc) XFreeGC(win->display, win->gc);
    if (win->window) XDestroyWindow(win->display, win->window);
    win->running = 0;
    free(win);
#endif
}

static void x11_pump_events(HVM_GuiBackendWindow* w) {
#if HAS_X11
    struct HVM_GuiBackendWindow* win = (struct HVM_GuiBackendWindow*)w;
    XEvent ev;
    
    if (!win || !win->display) return;
    
    while (XPending(win->display) > 0) {
        XNextEvent(win->display, &ev);
        
        switch (ev.type) {
            case ClientMessage:
                if ((Atom)ev.xclient.data.l[0] == XInternAtom(win->display, "WM_DELETE_WINDOW", False)) {
                    x11_push_event(win, X11_EVENT_QUIT, 0, 0, 0, 0);
                    win->running = 0;
                }
                break;
                
            case DestroyNotify:
                win->window = 0;
                win->running = 0;
                break;
                
            case Expose:
                if (ev.xexpose.count == 0) {
                    /* Redraw request - handled externally */
                }
                break;
                
            case ConfigureNotify:
                win->width = ev.xconfigure.width;
                win->height = ev.xconfigure.height;
                break;
                
            case KeyPress: {
                int ks = x11_keycode_to_keysym(ev.xkey.keycode);
                x11_push_event(win, X11_EVENT_KEY_DOWN, ks, ev.xkey.x, ev.xkey.y, 0);
                break;
            }
            case KeyRelease: {
                int ks = x11_keycode_to_keysym(ev.xkey.keycode);
                x11_push_event(win, X11_EVENT_KEY_UP, ks, ev.xkey.x, ev.xkey.y, 0);
                break;
            }
            case ButtonPress:
                x11_push_event(win, X11_EVENT_MOUSE_DOWN, 0, ev.xbutton.x, ev.xbutton.y, ev.xbutton.button);
                break;
            case ButtonRelease:
                x11_push_event(win, X11_EVENT_MOUSE_UP, 0, ev.xbutton.x, ev.xbutton.y, ev.xbutton.button);
                break;
            case MotionNotify:
                x11_push_event(win, X11_EVENT_MOUSE_MOVE, 0, ev.xmotion.x, ev.xmotion.y, 0);
                break;
        }
    }
    
    /* Dispatch queued events */
    while (x11_pop_event(win));
#endif
}

static void x11_request_repaint(HVM_GuiBackendWindow* w) {
#if HAS_X11
    struct HVM_GuiBackendWindow* win = (struct HVM_GuiBackendWindow*)w;
    if (!win || !win->window) return;
    XClearWindow(win->display, win->window);
    XFlush(win->display);
#endif
}

static double x11_now_ms(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec * 1000.0 + (double)ts.tv_nsec / 1000000.0;
}

static void x11_sleep_ms(int ms) {
    if (ms <= 0) return;
    struct timespec req;
    req.tv_sec = ms / 1000;
    req.tv_nsec = (long)(ms % 1000) * 1000000L;
    nanosleep(&req, NULL);
}

static unsigned long x11_rgb(int r, int g, int b) {
    return (unsigned long)(((r & 0xFF) << 16) | ((g & 0xFF) << 8) | (b & 0xFF));
}

static void x11_draw_text(HVM_GuiBackendWindow* w, int x, int y, const char* text,
                           int r, int g, int b, int size, int bold) {
#if HAS_X11
    struct HVM_GuiBackendWindow* win = (struct HVM_GuiBackendWindow*)w;
    if (!win || !win->window) return;
    if (!text) return;
    if (size <= 0) size = 16;

    /* Xft renders text at an arbitrary pixel size via scalable fonts
     * (fontconfig). The font is cached per (size, bold). */
    static XftFont* cached_font = NULL;
    static int cached_size = 0;
    static int cached_bold = 0;
    char pattern[128];

    if (!cached_font || cached_size != size || cached_bold != bold) {
        if (cached_font) { XftFontClose(win->display, cached_font); cached_font = NULL; }
        snprintf(pattern, sizeof(pattern), "sans:pixelsize=%d%s",
                 size, bold ? ":weight=bold" : "");
        cached_font = XftFontOpenName(win->display, win->screen, pattern);
        if (!cached_font) {
            snprintf(pattern, sizeof(pattern), "sans:pixelsize=%d", 16);
            cached_font = XftFontOpenName(win->display, win->screen, pattern);
        }
        if (!cached_font) return;
        cached_size = size;
        cached_bold = bold;
    }

    {
        Visual* vis = DefaultVisual(win->display, win->screen);
        Colormap cmap = DefaultColormap(win->display, win->screen);
        XftDraw* xd = XftDrawCreate(win->display, win->window, vis, cmap);
        if (!xd) return;
        XRenderColor xrc;
        XftColor col;
        xrc.red   = (unsigned short)((r & 0xFF) * 0x101);
        xrc.green = (unsigned short)((g & 0xFF) * 0x101);
        xrc.blue  = (unsigned short)((b & 0xFF) * 0x101);
        xrc.alpha = 0xffff;
        XftColorAllocValue(win->display, vis, cmap, &xrc, &col);
        XftDrawStringUtf8(xd, &col, cached_font, x,
                          y + (int)((double)size * 0.8),
                          (const FcChar8*)text, (int)strlen(text));
        XftColorFree(win->display, vis, cmap, &col);
        XftDrawDestroy(xd);
        XFlush(win->display);
    }
#endif
}

static void x11_draw_rect(HVM_GuiBackendWindow* w, int x, int y, int ww, int hh,
                           int r, int g, int b) {
#if HAS_X11
    struct HVM_GuiBackendWindow* win = (struct HVM_GuiBackendWindow*)w;
    if (!win || !win->window || !win->gc) return;
    XSetForeground(win->display, win->gc, x11_rgb(r, g, b));
    XFillRectangle(win->display, win->window, win->gc, x, y, ww, hh);
#endif
}

static void x11_draw_rect_outline(HVM_GuiBackendWindow* w, int x, int y, int ww, int hh,
                                   int r, int g, int b, int border_width) {
#if HAS_X11
    struct HVM_GuiBackendWindow* win = (struct HVM_GuiBackendWindow*)w;
    if (!win || !win->window || !win->gc) return;
    (void)border_width;
    XSetForeground(win->display, win->gc, x11_rgb(r, g, b));
    XDrawRectangle(win->display, win->window, win->gc, x, y, ww, hh);
#endif
}

static void x11_draw_round_rect(HVM_GuiBackendWindow* w, int x, int y, int ww, int hh, int radius,
                                 int fill_r, int fill_g, int fill_b,
                                 int border_r, int border_g, int border_b, int border_width) {
#if HAS_X11
    struct HVM_GuiBackendWindow* win = (struct HVM_GuiBackendWindow*)w;
    if (!win || !win->window || !win->gc) return;
    (void)radius;
    
    /* Fill */
    if (fill_r >= 0 || fill_g >= 0 || fill_b >= 0) {
        XSetForeground(win->display, win->gc, x11_rgb(fill_r, fill_g, fill_b));
        XFillRectangle(win->display, win->window, win->gc, x, y, ww, hh);
    }
    
    /* Border */
    if (border_width > 0) {
        XSetForeground(win->display, win->gc, x11_rgb(border_r, border_g, border_b));
        XDrawRectangle(win->display, win->window, win->gc, x, y, ww, hh);
    }
#endif
}

static int x11_host_is_lsb_first(void) {
    uint32_t probe = 1;
    return *(unsigned char*)&probe == 1;
}

/* Returns the bit position of the lowest set bit in mask, and via *out_bits
 * the number of contiguous set bits (channel width in that visual). */
static int x11_mask_shift(unsigned long mask, int* out_bits) {
    int shift = 0, bits = 0;
    if (!mask) { if (out_bits) *out_bits = 0; return 0; }
    while (!(mask & 1)) { mask >>= 1; shift++; }
    while (mask & 1) { mask >>= 1; bits++; }
    if (out_bits) *out_bits = bits;
    return shift;
}

static void x11_draw_image(HVM_GuiBackendWindow* w, int x, int y, int dst_w, int dst_h,
                            const char* image_path) {
#if HAS_X11
    struct HVM_GuiBackendWindow* win = (struct HVM_GuiBackendWindow*)w;
    unsigned char* src;
    int src_w, src_h, src_channels;
    Visual* visual;
    int depth, bytes_per_pixel;
    unsigned long rmask, gmask, bmask;
    int rshift, gshift, bshift, rbits, gbits, bbits;
    unsigned char* buf;
    int bytes_per_line;
    XImage* ximage;
    int i, j;

    if (!win || !win->window || !win->gc) return;
    if (!image_path || !image_path[0] || dst_w <= 0 || dst_h <= 0) return;

    {
        char resolved[1024];
        hosc_runtime_resolve_path(image_path, resolved, sizeof(resolved));
        src = stbi_load(resolved, &src_w, &src_h, &src_channels, 4);
        if (!src) {
            fprintf(stderr, "[GUI:x11] image skipped: cannot load \"%s\" (%s)\n",
                    resolved, stbi_failure_reason());
            return;
        }
    }

    visual = DefaultVisual(win->display, win->screen);
    depth = DefaultDepth(win->display, win->screen);
    rmask = visual->red_mask;
    gmask = visual->green_mask;
    bmask = visual->blue_mask;
    rshift = x11_mask_shift(rmask, &rbits);
    gshift = x11_mask_shift(gmask, &gbits);
    bshift = x11_mask_shift(bmask, &bbits);

    if (depth > 16) bytes_per_pixel = 4;
    else if (depth > 8) bytes_per_pixel = 2;
    else {
        /* Palette-based/low-color visuals aren't supported by this
         * simple blitter; bail out cleanly instead of drawing garbage. */
        stbi_image_free(src);
        fprintf(stderr, "[GUI:x11] image skipped: unsupported visual depth %d\n", depth);
        return;
    }

    bytes_per_line = dst_w * bytes_per_pixel;
    buf = (unsigned char*)malloc((size_t)bytes_per_line * (size_t)dst_h);
    if (!buf) {
        stbi_image_free(src);
        return;
    }

    for (j = 0; j < dst_h; j++) {
        int sy = (src_h == dst_h) ? j : (j * src_h) / dst_h;
        for (i = 0; i < dst_w; i++) {
            int sx = (src_w == dst_w) ? i : (i * src_w) / dst_w;
            const unsigned char* px = src + ((size_t)sy * src_w + sx) * 4;
            int r = px[0], g = px[1], b = px[2], a = px[3];
            unsigned long pixel;
            unsigned char* dst;

            /* Alpha-blend onto white, matching the window's white
             * background so transparent PNGs don't show garbage. */
            if (a < 255) {
                r = (r * a + 255 * (255 - a)) / 255;
                g = (g * a + 255 * (255 - a)) / 255;
                b = (b * a + 255 * (255 - a)) / 255;
            }

            pixel = ((unsigned long)(r * ((1 << rbits) - 1) / 255) << rshift) |
                    ((unsigned long)(g * ((1 << gbits) - 1) / 255) << gshift) |
                    ((unsigned long)(b * ((1 << bbits) - 1) / 255) << bshift);

            dst = buf + (size_t)j * bytes_per_line + (size_t)i * bytes_per_pixel;
            if (bytes_per_pixel == 4) memcpy(dst, &pixel, 4);
            else { uint16_t p16 = (uint16_t)pixel; memcpy(dst, &p16, 2); }
        }
    }
    stbi_image_free(src);

    ximage = XCreateImage(win->display, visual, (unsigned)depth, ZPixmap, 0,
                           (char*)buf, (unsigned)dst_w, (unsigned)dst_h,
                           bytes_per_pixel == 4 ? 32 : 16, bytes_per_line);
    if (!ximage) {
        free(buf);
        return;
    }
    /* We packed pixels in host byte order above; tell Xlib that's what
     * the buffer contains so it converts correctly for the wire. */
    ximage->byte_order = x11_host_is_lsb_first() ? LSBFirst : MSBFirst;

    XPutImage(win->display, win->window, win->gc, ximage, 0, 0, x, y, dst_w, dst_h);
    XDestroyImage(ximage); /* also frees buf via free() */
#else
    (void)w; (void)x; (void)y; (void)dst_w; (void)dst_h; (void)image_path;
#endif
}

static int x11_get_width(HVM_GuiBackendWindow* w) {
#if HAS_X11
    struct HVM_GuiBackendWindow* win = (struct HVM_GuiBackendWindow*)w;
    return win ? win->width : 800;
#else
    (void)w;
    return 800;
#endif
}

static int x11_get_height(HVM_GuiBackendWindow* w) {
#if HAS_X11
    struct HVM_GuiBackendWindow* win = (struct HVM_GuiBackendWindow*)w;
    return win ? win->height : 600;
#else
    (void)w;
    return 600;
#endif
}

static int x11_get_scroll_y(HVM_GuiBackendWindow* w) {
#if HAS_X11
    struct HVM_GuiBackendWindow* win = (struct HVM_GuiBackendWindow*)w;
    return win ? win->scroll_y : 0;
#else
    (void)w;
    return 0;
#endif
}

static void x11_set_scroll_range(HVM_GuiBackendWindow* w, int range) {
#if HAS_X11
    struct HVM_GuiBackendWindow* win = (struct HVM_GuiBackendWindow*)w;
    if (win) win->scroll_range = range;
#else
    (void)w; (void)range;
#endif
}

static int x11_is_running(HVM_GuiBackendWindow* w) {
#if HAS_X11
    struct HVM_GuiBackendWindow* win = (struct HVM_GuiBackendWindow*)w;
    return win ? win->running : 0;
#else
    (void)w;
    return 0;
#endif
}

HVM_GuiBackend hvm_gui_backend_x11 = {
    .name = "x11",
    .available = x11_available,
    .init = x11_init,
    .shutdown = x11_shutdown,
    .create_window = x11_create_window,
    .destroy_window = x11_destroy_window,
    .pump_events = x11_pump_events,
    .request_repaint = x11_request_repaint,
    .now_ms = x11_now_ms,
    .sleep_ms = x11_sleep_ms,
    .draw_text = x11_draw_text,
    .draw_image = x11_draw_image,
    .draw_rect = x11_draw_rect,
    .draw_rect_outline = x11_draw_rect_outline,
    .draw_round_rect = x11_draw_round_rect,
    .get_width = x11_get_width,
    .get_height = x11_get_height,
    .get_scroll_y = x11_get_scroll_y,
    .set_scroll_range = x11_set_scroll_range,
    .is_running = x11_is_running
};