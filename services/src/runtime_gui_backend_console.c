/* runtime_gui_backend_console.c - Console (fallback) GUI backend */
#ifndef _POSIX_C_SOURCE
#define _POSIX_C_SOURCE 200809L
#endif
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <stdint.h>

#ifdef _WIN32
#include <windows.h>
#else
#include <unistd.h>
#endif

#include "runtime_gui_backend.h"

static int console_available(void) {
    return 1; /* Always available */
}

static int console_init(void) {
    return 1;
}

static void console_shutdown(void) {
}

static HVM_GuiBackendWindow* console_create_window(const char* title, int width, int height,
                                                     int resizable, int fullscreen, const char* icon,
                                                     int min_width, int min_height, int center,
                                                     HVM_GuiEventCallback callback, void* userdata) {
    (void)title; (void)width; (void)height; (void)resizable; (void)fullscreen;
    (void)icon; (void)min_width; (void)min_height; (void)center;
    (void)callback; (void)userdata;
    printf("[GUI] Console window: %s (%dx%d)\n", title ? title : "", width, height);
    return (HVM_GuiBackendWindow*)(uintptr_t)1; /* Dummy non-NULL handle */
}

static void console_destroy_window(HVM_GuiBackendWindow* window) {
    (void)window;
}

static void console_pump_events(HVM_GuiBackendWindow* window) {
    (void)window;
}

static void console_request_repaint(HVM_GuiBackendWindow* window) {
    (void)window;
}

static double console_now_ms(void) {
#ifdef _WIN32
    return (double)GetTickCount64();
#else
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec * 1000.0 + (double)ts.tv_nsec / 1000000.0;
#endif
}

static void console_sleep_ms(int ms) {
    if (ms <= 0) return;
#ifdef _WIN32
    Sleep((DWORD)ms);
#else
    struct timespec req;
    req.tv_sec = ms / 1000;
    req.tv_nsec = (long)(ms % 1000) * 1000000L;
    nanosleep(&req, NULL);
#endif
}

static void console_draw_text(HVM_GuiBackendWindow* window, int x, int y, const char* text,
                               int r, int g, int b, int size, int bold) {
    (void)window; (void)x; (void)y; (void)r; (void)g; (void)b; (void)size; (void)bold;
    printf("[GUI] text: %s\n", text ? text : "");
}

static void console_draw_rect(HVM_GuiBackendWindow* window, int x, int y, int w, int h,
                               int r, int g, int b) {
    (void)window; (void)x; (void)y; (void)w; (void)h; (void)r; (void)g; (void)b;
}

static void console_draw_rect_outline(HVM_GuiBackendWindow* window, int x, int y, int w, int h,
                                       int r, int g, int b, int border_width) {
    (void)window; (void)x; (void)y; (void)w; (void)h; (void)r; (void)g; (void)b; (void)border_width;
}

static void console_draw_round_rect(HVM_GuiBackendWindow* window, int x, int y, int w, int h, int radius,
                                     int fill_r, int fill_g, int fill_b,
                                     int border_r, int border_g, int border_b, int border_width) {
    (void)window; (void)x; (void)y; (void)w; (void)h; (void)radius;
    (void)fill_r; (void)fill_g; (void)fill_b;
    (void)border_r; (void)border_g; (void)border_b; (void)border_width;
}

static int console_get_width(HVM_GuiBackendWindow* window) {
    (void)window;
    return 80;
}

static int console_get_height(HVM_GuiBackendWindow* window) {
    (void)window;
    return 24;
}

static int console_get_scroll_y(HVM_GuiBackendWindow* window) {
    (void)window;
    return 0;
}

static void console_set_scroll_range(HVM_GuiBackendWindow* window, int range) {
    (void)window; (void)range;
}

static int console_is_running(HVM_GuiBackendWindow* window) {
    (void)window;
    return 1;
}

HVM_GuiBackend hvm_gui_backend_console = {
    .name = "console",
    .available = console_available,
    .init = console_init,
    .shutdown = console_shutdown,
    .create_window = console_create_window,
    .destroy_window = console_destroy_window,
    .pump_events = console_pump_events,
    .request_repaint = console_request_repaint,
    .now_ms = console_now_ms,
    .sleep_ms = console_sleep_ms,
    .draw_text = console_draw_text,
    .draw_rect = console_draw_rect,
    .draw_rect_outline = console_draw_rect_outline,
    .draw_round_rect = console_draw_round_rect,
    .get_width = console_get_width,
    .get_height = console_get_height,
    .get_scroll_y = console_get_scroll_y,
    .set_scroll_range = console_set_scroll_range,
    .is_running = console_is_running
};