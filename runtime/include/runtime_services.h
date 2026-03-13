/*
 * File: runtime/include/runtime_services.h
 * Purpose: Defines the runtime services interface for GUI and platform services.
 */

#ifndef RUNTIME_SERVICES_H
#define RUNTIME_SERVICES_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stddef.h>

typedef struct HVM_RuntimeServices HVM_RuntimeServices;

typedef struct {
    int (*create_window)(HVM_RuntimeServices *services, const char *title);
    void (*shutdown)(HVM_RuntimeServices *services);
    void (*prepare_run)(HVM_RuntimeServices *services);
    void (*finish_run)(HVM_RuntimeServices *services);
    void (*clear_commands)(HVM_RuntimeServices *services);
    void (*set_bg_color)(HVM_RuntimeServices *services, unsigned int color);
    void (*set_fg_color)(HVM_RuntimeServices *services, unsigned int color);
    void (*set_font_size)(HVM_RuntimeServices *services, int size);
    int (*draw_text)(HVM_RuntimeServices *services, int x, int y, const char *text);
    int (*draw_button)(HVM_RuntimeServices *services, int x, int y, int w, int h, const char *label, int *clicked);
    int (*draw_button_state)(HVM_RuntimeServices *services, const char *id, const char *label, int *clicked);
    int (*draw_input)(HVM_RuntimeServices *services, int x, int y, int w, const char *label, const char **out_text);
    int (*draw_input_state)(HVM_RuntimeServices *services, const char *id, const char *placeholder, const char **out_text);
    int (*draw_textarea)(HVM_RuntimeServices *services, int x, int y, int w, int h, const char *id, const char **out_text);
    int (*draw_image)(HVM_RuntimeServices *services, int x, int y, int w, int h, const char *label);
    int (*get_mouse_x)(HVM_RuntimeServices *services);
    int (*get_mouse_y)(HVM_RuntimeServices *services);
    int (*is_mouse_down)(HVM_RuntimeServices *services);
    int (*was_mouse_up)(HVM_RuntimeServices *services);
    int (*was_mouse_click)(HVM_RuntimeServices *services);
    int (*is_mouse_hover)(HVM_RuntimeServices *services, const char *id);
    int (*is_key_down)(HVM_RuntimeServices *services, int key);
    int (*was_key_press)(HVM_RuntimeServices *services, int key);
    double (*get_delta_ms)(HVM_RuntimeServices *services);
    int (*layout_reset)(HVM_RuntimeServices *services, int x, int y, int gap);
    int (*layout_next)(HVM_RuntimeServices *services, int height);
    int (*layout_row)(HVM_RuntimeServices *services, int cols);
    int (*layout_column)(HVM_RuntimeServices *services, int width);
    int (*layout_grid)(HVM_RuntimeServices *services, int cols, int cell_w, int cell_h);
    int (*loop_tick)(HVM_RuntimeServices *services, double *delta_ms);
    int (*menu_setup_notepad)(HVM_RuntimeServices *services);
    int (*menu_event)(HVM_RuntimeServices *services);
    int (*scroll_set_range)(HVM_RuntimeServices *services, int range);
    int (*scroll_y)(HVM_RuntimeServices *services);
    char *(*open_file_dialog)(HVM_RuntimeServices *services);
    char *(*save_file_dialog)(HVM_RuntimeServices *services);
    int (*input_set)(HVM_RuntimeServices *services, const char *label, const char *text);
    int (*textarea_set)(HVM_RuntimeServices *services, const char *id, const char *text);
} HVM_GuiServices;

struct HVM_RuntimeServices {
    void *context;
    HVM_GuiServices gui;
};

HVM_RuntimeServices *hvm_runtime_services_create_default(void);
void hvm_runtime_services_destroy(HVM_RuntimeServices *services);

#ifdef __cplusplus
}
#endif

#endif