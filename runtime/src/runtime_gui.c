/*
 * File: runtime/src/runtime_gui.c
 * Purpose: Implements GUI services and state for the HVM runtime.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <stdint.h>
#include "runtime_services.h"
#include "runtime_gui.h"

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <windowsx.h>
#include <commdlg.h>
#endif

#ifndef _WIN32
#define RGB(r,g,b) (((unsigned int)(r) & 0xFF) | (((unsigned int)(g) & 0xFF) << 8) | (((unsigned int)(b) & 0xFF) << 16))
#endif

#define GUI_NO_ACTIVE_INPUT ((size_t)(-1))
#define GUI_NO_ACTIVE_TEXTAREA ((size_t)(-1))
#define GUI_PAYLOAD_INDEX(i) ((void *)(uintptr_t)((i) + 1))
#define GUI_PAYLOAD_TO_INDEX(p) ((size_t)((uintptr_t)(p) - 1))

#define HVM_MENU_FILE_NEW 1001
#define HVM_MENU_FILE_OPEN 1002
#define HVM_MENU_FILE_SAVE 1003
#define HVM_MENU_FILE_EXIT 1004

typedef enum {
    GUI_LAYOUT_FLOW = 0,
    GUI_LAYOUT_ROW_MODE,
    GUI_LAYOUT_COLUMN_MODE,
    GUI_LAYOUT_GRID_MODE
} HVM_GuiLayoutMode;

typedef enum {
    GUI_CMD_TEXT,
    GUI_CMD_BUTTON,
    GUI_CMD_INPUT,
    GUI_CMD_TEXTAREA,
    GUI_CMD_IMAGE
} HVM_GuiCmdType;

typedef struct {
    HVM_GuiCmdType type;
    int x;
    int y;
    int w;
    int h;
    char *text;
    void *payload;
    unsigned int color;
} HVM_GuiCmd;

typedef struct {
    char *label;
    char *buffer;
    size_t len;
    size_t cap;
} HVM_InputState;

typedef struct {
    char *id;
    char *buffer;
    size_t len;
    size_t cap;
} HVM_TextAreaState;

typedef struct {
    char *id;
    int x;
    int y;
    int w;
    int h;
    int hovered;
    int down;
    int clicked;
    uint64_t frame_seen;
} HVM_WidgetState;

struct HVM_GuiState {
#ifdef _WIN32
    HWND hwnd;
    HFONT font;
#endif
    HVM_GuiCmd *cmds;
    size_t cmd_count;
    size_t cmd_cap;

    HVM_InputState *inputs;
    size_t input_count;
    size_t input_cap;
    size_t active_input;

    HVM_TextAreaState *textareas;
    size_t textarea_count;
    size_t textarea_cap;
    size_t active_textarea;

    HVM_WidgetState *widgets;
    size_t widget_count;
    size_t widget_cap;

    unsigned int fg_color;
    unsigned int bg_color;
    int font_size;

    int mouse_x;
    int mouse_y;
    int mouse_down;
    int mouse_up;
    int mouse_clicked;
    int mouse_click_consumed;
    int mouse_click_x;
    int mouse_click_y;

    int key_down[256];
    int key_pressed[256];

    double delta_ms;
    uint64_t last_tick;
    uint64_t frame_index;

    int layout_x;
    int layout_y;
    int layout_base_x;
    int layout_base_y;
    int layout_gap;
    int layout_mode;
    int layout_index;
    int layout_cols;
    int layout_col_width;
    int layout_row_height;
    int layout_grid_cell_w;
    int layout_grid_cell_h;

    int scroll_y;
    int scroll_range;
    int menu_ready;
    int last_menu_cmd;

    int using_real;
    int running;
    int loop_called;

    int class_registered;
};

static const char *HVM_GUI_CLASS_NAME = "HOSCVMWindowClass";

static HVM_GuiState *gui_from_services(HVM_RuntimeServices *services) {
    if (!services) return NULL;
    return (HVM_GuiState *)services->context;
}

static int gui_debug_enabled(void) {
    const char *env = getenv("HVM_GUI_DEBUG");
    if (!env || env[0] == '\0') return 0;
    if (strcmp(env, "1") == 0 || strcmp(env, "true") == 0 || strcmp(env, "yes") == 0) return 1;
    return 0;
}

#define GUI_LOG(gui, ...) do { if (!(gui)->using_real || gui_debug_enabled()) printf(__VA_ARGS__); } while (0)
static void gui_free_commands(HVM_GuiState *gui) {
    size_t i;
    if (!gui) return;
    for (i = 0; i < gui->cmd_count; i++) {
        free(gui->cmds[i].text);
        gui->cmds[i].text = NULL;
    }
    free(gui->cmds);
    gui->cmds = NULL;
    gui->cmd_count = 0;
    gui->cmd_cap = 0;
}

static void gui_clear_commands_internal(HVM_GuiState *gui) {
    size_t i;
    if (!gui) return;
    for (i = 0; i < gui->cmd_count; i++) {
        free(gui->cmds[i].text);
        gui->cmds[i].text = NULL;
    }
    gui->cmd_count = 0;
}

static int gui_buffer_set(char **buffer, size_t *len, size_t *cap, const char *value) {
    size_t n;
    char *nb;
    if (!buffer || !len || !cap) return 0;
    if (!value) value = "";
    n = strlen(value);
    if (*cap < n + 1) {
        nb = (char *)realloc(*buffer, n + 1);
        if (!nb) return 0;
        *buffer = nb;
        *cap = n + 1;
    }
    memcpy(*buffer, value, n + 1);
    *len = n;
    return 1;
}

static int gui_buffer_append_char(char **buffer, size_t *len, size_t *cap, char ch) {
    char *nb;
    size_t new_cap;
    if (!buffer || !len || !cap) return 0;
    if (*cap == 0) {
        *buffer = (char *)calloc(1, 1);
        if (!*buffer) return 0;
        *cap = 1;
        *len = 0;
    }
    if (*len + 2 > *cap) {
        new_cap = (*cap < 16) ? 16 : (*cap * 2);
        while (*len + 2 > new_cap) new_cap *= 2;
        nb = (char *)realloc(*buffer, new_cap);
        if (!nb) return 0;
        *buffer = nb;
        *cap = new_cap;
    }
    (*buffer)[*len] = ch;
    (*len)++;
    (*buffer)[*len] = '\0';
    return 1;
}

static void gui_free_inputs(HVM_GuiState *gui) {
    size_t i;
    if (!gui) return;
    for (i = 0; i < gui->input_count; i++) {
        free(gui->inputs[i].label);
        free(gui->inputs[i].buffer);
    }
    free(gui->inputs);
    gui->inputs = NULL;
    gui->input_count = 0;
    gui->input_cap = 0;
    gui->active_input = GUI_NO_ACTIVE_INPUT;
}

static void gui_free_textareas(HVM_GuiState *gui) {
    size_t i;
    if (!gui) return;
    for (i = 0; i < gui->textarea_count; i++) {
        free(gui->textareas[i].id);
        free(gui->textareas[i].buffer);
    }
    free(gui->textareas);
    gui->textareas = NULL;
    gui->textarea_count = 0;
    gui->textarea_cap = 0;
    gui->active_textarea = GUI_NO_ACTIVE_TEXTAREA;
}

static void gui_free_widgets(HVM_GuiState *gui) {
    size_t i;
    if (!gui) return;
    for (i = 0; i < gui->widget_count; i++) {
        free(gui->widgets[i].id);
        gui->widgets[i].id = NULL;
    }
    free(gui->widgets);
    gui->widgets = NULL;
    gui->widget_count = 0;
    gui->widget_cap = 0;
}

static HVM_WidgetState* gui_find_widget_state(HVM_GuiState *gui, const char *id) {
    size_t i;
    if (!gui) return NULL;
    if (!id) id = "";
    for (i = 0; i < gui->widget_count; i++) {
        if (gui->widgets[i].id && strcmp(gui->widgets[i].id, id) == 0) {
            return &gui->widgets[i];
        }
    }
    return NULL;
}

static HVM_WidgetState* gui_get_widget_state(HVM_GuiState *gui, const char *id) {
    HVM_WidgetState *state;
    if (!gui) return NULL;
    if (!id) id = "";

    state = gui_find_widget_state(gui, id);
    if (state) return state;

    if (gui->widget_count == gui->widget_cap) {
        size_t new_cap = gui->widget_cap == 0 ? 16 : gui->widget_cap * 2;
        HVM_WidgetState *nw = (HVM_WidgetState *)realloc(gui->widgets, new_cap * sizeof(HVM_WidgetState));
        if (!nw) return NULL;
        memset(nw + gui->widget_cap, 0, (new_cap - gui->widget_cap) * sizeof(HVM_WidgetState));
        gui->widgets = nw;
        gui->widget_cap = new_cap;
    }

    gui->widgets[gui->widget_count].id = strdup(id);
    if (!gui->widgets[gui->widget_count].id) return NULL;

    gui->widgets[gui->widget_count].x = 0;
    gui->widgets[gui->widget_count].y = 0;
    gui->widgets[gui->widget_count].w = 0;
    gui->widgets[gui->widget_count].h = 0;
    gui->widgets[gui->widget_count].hovered = 0;
    gui->widgets[gui->widget_count].down = 0;
    gui->widgets[gui->widget_count].clicked = 0;
    gui->widgets[gui->widget_count].frame_seen = 0;

    gui->widget_count++;
    return &gui->widgets[gui->widget_count - 1];
}

static int gui_point_in_rect(int px, int py, int x, int y, int w, int h) {
    if (w < 0) w = 0;
    if (h < 0) h = 0;
    return px >= x && py >= y && px <= x + w && py <= y + h;
}

static int gui_take_mouse_click_in_rect(HVM_GuiState *gui, int x, int y, int w, int h) {
    if (!gui) return 0;
    if (!gui->mouse_clicked || gui->mouse_click_consumed) return 0;
    if (!gui_point_in_rect(gui->mouse_click_x, gui->mouse_click_y, x, y, w, h)) return 0;
    gui->mouse_click_consumed = 1;
    return 1;
}

static int gui_take_mouse_focus_in_rect(HVM_GuiState *gui, int x, int y, int w, int h) {
    if (!gui) return 0;
    if (gui_take_mouse_click_in_rect(gui, x, y, w, h)) return 1;
    if (gui->mouse_down && gui_point_in_rect(gui->mouse_x, gui->mouse_y, x, y, w, h)) {
        return 1;
    }
    return 0;
}

static void gui_reset_transient_input_internal(HVM_GuiState *gui) {
    if (!gui) return;
    gui->mouse_up = 0;
    gui->mouse_clicked = 0;
    gui->mouse_click_consumed = 0;
    memset(gui->key_pressed, 0, sizeof(gui->key_pressed));
}

static void gui_apply_click_focus_from_commands(HVM_GuiState *gui) {
    size_t i;

    if (!gui) return;
    if (!gui->mouse_clicked || gui->mouse_click_consumed) {
        return;
    }

    for (i = gui->cmd_count; i > 0; i--) {
        HVM_GuiCmd *cmd = &gui->cmds[i - 1];

        if (!gui_point_in_rect(gui->mouse_click_x, gui->mouse_click_y, cmd->x, cmd->y, cmd->w, cmd->h)) {
            continue;
        }

        if (cmd->type == GUI_CMD_INPUT) {
            size_t idx = GUI_PAYLOAD_TO_INDEX(cmd->payload);
            if (idx < gui->input_count) {
                gui->active_input = idx;
                gui->active_textarea = GUI_NO_ACTIVE_TEXTAREA;
                gui->mouse_click_consumed = 1;
                return;
            }
        }

        if (cmd->type == GUI_CMD_TEXTAREA) {
            size_t idx = GUI_PAYLOAD_TO_INDEX(cmd->payload);
            if (idx < gui->textarea_count) {
                gui->active_textarea = idx;
                gui->active_input = GUI_NO_ACTIVE_INPUT;
                gui->mouse_click_consumed = 1;
                return;
            }
        }
    }
}

static HVM_WidgetState* gui_register_widget_rect(HVM_GuiState *gui, const char *id, int x, int y, int w, int h) {
    HVM_WidgetState *state;
    if (!gui) return NULL;
    state = gui_get_widget_state(gui, id);
    if (!state) return NULL;

    state->x = x;
    state->y = y;
    state->w = w;
    state->h = h;
    state->hovered = gui_point_in_rect(gui->mouse_x, gui->mouse_y, x, y, w, h);
    state->down = state->hovered && gui->mouse_down;
    state->clicked = gui_point_in_rect(gui->mouse_click_x, gui->mouse_click_y, x, y, w, h) && gui->mouse_clicked;
    state->frame_seen = gui->frame_index;
    return state;
}

static void gui_layout_place_widget(HVM_GuiState *gui, int pref_w, int pref_h, int *x, int *y, int *w, int *h) {
    int px;
    int py;
    int pw;
    int ph;

    if (!gui) return;
    if (pref_w < 1) pref_w = 160;
    if (pref_h < 1) pref_h = 32;

    px = gui->layout_x;
    py = gui->layout_y;
    pw = pref_w;
    ph = pref_h;

    switch ((HVM_GuiLayoutMode)gui->layout_mode) {
        case GUI_LAYOUT_ROW_MODE: {
            int cols = gui->layout_cols > 0 ? gui->layout_cols : 1;
            int col = gui->layout_index % cols;
            int row = gui->layout_index / cols;
            int cell_w = gui->layout_col_width > 0 ? gui->layout_col_width : pref_w;
            int cell_h = gui->layout_row_height > 0 ? gui->layout_row_height : pref_h;
            px = gui->layout_base_x + col * (cell_w + gui->layout_gap);
            py = gui->layout_base_y + row * (cell_h + gui->layout_gap);
            pw = cell_w;
            ph = cell_h;
            gui->layout_index++;
            break;
        }
        case GUI_LAYOUT_COLUMN_MODE: {
            int cell_w = gui->layout_col_width > 0 ? gui->layout_col_width : pref_w;
            px = gui->layout_base_x;
            py = gui->layout_base_y + gui->layout_index * (pref_h + gui->layout_gap);
            pw = cell_w;
            gui->layout_index++;
            break;
        }
        case GUI_LAYOUT_GRID_MODE: {
            int cols = gui->layout_cols > 0 ? gui->layout_cols : 2;
            int col = gui->layout_index % cols;
            int row = gui->layout_index / cols;
            int cell_w = gui->layout_grid_cell_w > 0 ? gui->layout_grid_cell_w : pref_w;
            int cell_h = gui->layout_grid_cell_h > 0 ? gui->layout_grid_cell_h : pref_h;
            px = gui->layout_base_x + col * (cell_w + gui->layout_gap);
            py = gui->layout_base_y + row * (cell_h + gui->layout_gap);
            pw = cell_w;
            ph = cell_h;
            gui->layout_index++;
            break;
        }
        case GUI_LAYOUT_FLOW:
        default:
            gui->layout_y += pref_h + gui->layout_gap;
            gui->layout_index++;
            break;
    }

    gui->layout_x = px;
    gui->layout_y = py + ph + gui->layout_gap;

    if (x) *x = px;
    if (y) *y = py;
    if (w) *w = pw;
    if (h) *h = ph;
}
static HVM_InputState* gui_get_input_state(HVM_GuiState *gui, const char *label) {
    size_t i;
    if (!gui) return NULL;
    if (!label) label = "";
    for (i = 0; i < gui->input_count; i++) {
        if (gui->inputs[i].label && strcmp(gui->inputs[i].label, label) == 0) {
            return &gui->inputs[i];
        }
    }

    if (gui->input_count == gui->input_cap) {
        size_t new_cap = gui->input_cap == 0 ? 8 : gui->input_cap * 2;
        HVM_InputState *ni = (HVM_InputState *)realloc(gui->inputs, new_cap * sizeof(HVM_InputState));
        if (!ni) return NULL;
        memset(ni + gui->input_cap, 0, (new_cap - gui->input_cap) * sizeof(HVM_InputState));
        gui->inputs = ni;
        gui->input_cap = new_cap;
    }

    gui->inputs[gui->input_count].label = strdup(label);
    if (!gui->inputs[gui->input_count].label) return NULL;
    gui->inputs[gui->input_count].buffer = (char *)calloc(1, 1);
    if (!gui->inputs[gui->input_count].buffer) {
        free(gui->inputs[gui->input_count].label);
        gui->inputs[gui->input_count].label = NULL;
        return NULL;
    }
    gui->inputs[gui->input_count].len = 0;
    gui->inputs[gui->input_count].cap = 1;
    gui->input_count++;
    return &gui->inputs[gui->input_count - 1];
}

static HVM_TextAreaState* gui_get_textarea_state(HVM_GuiState *gui, const char *id) {
    size_t i;
    if (!gui) return NULL;
    if (!id) id = "";

    for (i = 0; i < gui->textarea_count; i++) {
        if (gui->textareas[i].id && strcmp(gui->textareas[i].id, id) == 0) {
            return &gui->textareas[i];
        }
    }

    if (gui->textarea_count == gui->textarea_cap) {
        size_t new_cap = gui->textarea_cap == 0 ? 4 : gui->textarea_cap * 2;
        HVM_TextAreaState *nt = (HVM_TextAreaState *)realloc(gui->textareas, new_cap * sizeof(HVM_TextAreaState));
        if (!nt) return NULL;
        memset(nt + gui->textarea_cap, 0, (new_cap - gui->textarea_cap) * sizeof(HVM_TextAreaState));
        gui->textareas = nt;
        gui->textarea_cap = new_cap;
    }

    gui->textareas[gui->textarea_count].id = strdup(id);
    if (!gui->textareas[gui->textarea_count].id) return NULL;
    gui->textareas[gui->textarea_count].buffer = (char *)calloc(1, 1);
    if (!gui->textareas[gui->textarea_count].buffer) {
        free(gui->textareas[gui->textarea_count].id);
        gui->textareas[gui->textarea_count].id = NULL;
        return NULL;
    }
    gui->textareas[gui->textarea_count].len = 0;
    gui->textareas[gui->textarea_count].cap = 1;
    gui->textarea_count++;
    return &gui->textareas[gui->textarea_count - 1];
}

static int gui_push_cmd(HVM_GuiState *gui, const HVM_GuiCmd *cmd) {
    size_t new_cap;
    HVM_GuiCmd *nc;
    if (!gui || !cmd) return 0;
    if (gui->cmd_count == gui->cmd_cap) {
        new_cap = gui->cmd_cap == 0 ? 32 : gui->cmd_cap * 2;
        nc = (HVM_GuiCmd *)realloc(gui->cmds, new_cap * sizeof(HVM_GuiCmd));
        if (!nc) return 0;
        memset(nc + gui->cmd_cap, 0, (new_cap - gui->cmd_cap) * sizeof(HVM_GuiCmd));
        gui->cmds = nc;
        gui->cmd_cap = new_cap;
    }
    gui->cmds[gui->cmd_count] = *cmd;
    if (cmd->text) {
        gui->cmds[gui->cmd_count].text = strdup(cmd->text);
        if (!gui->cmds[gui->cmd_count].text) return 0;
    }
    gui->cmd_count++;
    return 1;
}

static double gui_now_ms(void) {
#ifdef _WIN32
    LARGE_INTEGER freq, ctr;
    QueryPerformanceFrequency(&freq);
    QueryPerformanceCounter(&ctr);
    return (double)ctr.QuadPart * 1000.0 / (double)freq.QuadPart;
#else
    return (double)clock() * 1000.0 / (double)CLOCKS_PER_SEC;
#endif
}

static void gui_reset_style_defaults(HVM_GuiState *gui) {
    if (!gui) return;
    gui->fg_color = 0x000000;
    gui->bg_color = 0x00FFFFFF;
    gui->font_size = 18;

    gui->layout_x = 16;
    gui->layout_y = 16;
    gui->layout_base_x = 16;
    gui->layout_base_y = 16;
    gui->layout_gap = 8;
    gui->layout_mode = GUI_LAYOUT_FLOW;
    gui->layout_index = 0;
    gui->layout_cols = 2;
    gui->layout_col_width = 180;
    gui->layout_row_height = 32;
    gui->layout_grid_cell_w = 180;
    gui->layout_grid_cell_h = 32;

    gui->scroll_y = 0;
    gui->scroll_range = 0;
    gui->menu_ready = 0;
    gui->last_menu_cmd = 0;

    gui->active_input = GUI_NO_ACTIVE_INPUT;
    gui->active_textarea = GUI_NO_ACTIVE_TEXTAREA;

    gui->mouse_down = 0;
    gui->mouse_up = 0;
    gui->mouse_clicked = 0;
    gui->mouse_click_consumed = 0;
    gui->mouse_click_x = 0;
    gui->mouse_click_y = 0;
    gui->mouse_x = 0;
    gui->mouse_y = 0;

    memset(gui->key_down, 0, sizeof(gui->key_down));
    memset(gui->key_pressed, 0, sizeof(gui->key_pressed));

    if (gui->frame_index == 0) gui->frame_index = 1;

#ifdef _WIN32
    if (gui->font) {
        DeleteObject(gui->font);
        gui->font = NULL;
    }
    gui->font = CreateFontA(gui->font_size, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, ANSI_CHARSET,
                             OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, DEFAULT_QUALITY,
                             DEFAULT_PITCH | FF_DONTCARE, "Segoe UI");
#endif
}
#ifdef _WIN32
static LRESULT CALLBACK hvm_gui_wndproc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam);

static int gui_headless_mode(void) {
    const char *env = getenv("HVM_GUI_HEADLESS");
    if (!env || env[0] == '\0') return 0;
    if (strcmp(env, "1") == 0 || strcmp(env, "true") == 0 || strcmp(env, "yes") == 0) return 1;
    return 0;
}

static int gui_register_class(HVM_GuiState *gui) {
    WNDCLASSA wc;
    if (!gui) return 0;
    if (gui->class_registered) return 1;

    memset(&wc, 0, sizeof(wc));
    wc.lpfnWndProc = hvm_gui_wndproc;
    wc.hInstance = GetModuleHandleA(NULL);
    wc.lpszClassName = HVM_GUI_CLASS_NAME;
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);
    wc.hbrBackground = NULL;

    if (!RegisterClassA(&wc)) {
        return 0;
    }

    gui->class_registered = 1;
    return 1;
}

static void gui_update_scrollbar(HVM_GuiState *gui) {
    RECT client;
    SCROLLINFO si;
    int page;
    int max_pos;

    if (!gui || !gui->hwnd) return;

    GetClientRect(gui->hwnd, &client);
    page = client.bottom - client.top;
    if (page < 1) page = 1;

    if (gui->scroll_range < 0) gui->scroll_range = 0;
    max_pos = gui->scroll_range - page;
    if (max_pos < 0) max_pos = 0;

    if (gui->scroll_y < 0) gui->scroll_y = 0;
    if (gui->scroll_y > max_pos) gui->scroll_y = max_pos;

    memset(&si, 0, sizeof(si));
    si.cbSize = sizeof(si);
    si.fMask = SIF_RANGE | SIF_PAGE | SIF_POS;
    si.nMin = 0;
    si.nMax = gui->scroll_range;
    si.nPage = (UINT)page;
    si.nPos = gui->scroll_y;
    SetScrollInfo(gui->hwnd, SB_VERT, &si, TRUE);
}

static void gui_scroll_to(HVM_GuiState *gui, int new_pos) {
    if (!gui) return;
    gui->scroll_y = new_pos;
    gui_update_scrollbar(gui);
}

static void gui_handle_vscroll(HVM_GuiState *gui, WPARAM wparam) {
    SCROLLINFO si;
    int pos;
    int max_pos;

    if (!gui || !gui->hwnd) return;

    memset(&si, 0, sizeof(si));
    si.cbSize = sizeof(si);
    si.fMask = SIF_ALL;
    GetScrollInfo(gui->hwnd, SB_VERT, &si);

    pos = si.nPos;
    switch (LOWORD(wparam)) {
        case SB_TOP: pos = 0; break;
        case SB_BOTTOM: pos = si.nMax; break;
        case SB_LINEUP: pos -= 24; break;
        case SB_LINEDOWN: pos += 24; break;
        case SB_PAGEUP: pos -= (int)si.nPage; break;
        case SB_PAGEDOWN: pos += (int)si.nPage; break;
        case SB_THUMBPOSITION:
        case SB_THUMBTRACK:
            pos = si.nTrackPos;
            break;
        default:
            break;
    }

    max_pos = si.nMax - (int)si.nPage + 1;
    if (max_pos < 0) max_pos = 0;
    if (pos < 0) pos = 0;
    if (pos > max_pos) pos = max_pos;

    if (pos != gui->scroll_y) {
        gui_scroll_to(gui, pos);
        InvalidateRect(gui->hwnd, NULL, FALSE);
    }
}

static int gui_menu_setup_notepad_internal(HVM_GuiState *gui) {
    HMENU old_menu;
    HMENU menu_bar;
    HMENU file_menu;

    if (!gui || !gui->hwnd) return 0;

    old_menu = GetMenu(gui->hwnd);
    if (old_menu) {
        SetMenu(gui->hwnd, NULL);
        DestroyMenu(old_menu);
    }

    menu_bar = CreateMenu();
    file_menu = CreatePopupMenu();
    if (!menu_bar || !file_menu) {
        if (file_menu) DestroyMenu(file_menu);
        if (menu_bar) DestroyMenu(menu_bar);
        return 0;
    }

    AppendMenuA(file_menu, MF_STRING, HVM_MENU_FILE_NEW, "New");
    AppendMenuA(file_menu, MF_STRING, HVM_MENU_FILE_OPEN, "Open...");
    AppendMenuA(file_menu, MF_STRING, HVM_MENU_FILE_SAVE, "Save...");
    AppendMenuA(file_menu, MF_SEPARATOR, 0, NULL);
    AppendMenuA(file_menu, MF_STRING, HVM_MENU_FILE_EXIT, "Exit");
    AppendMenuA(menu_bar, MF_POPUP, (UINT_PTR)file_menu, "File");

    SetMenu(gui->hwnd, menu_bar);
    DrawMenuBar(gui->hwnd);
    gui->menu_ready = 1;
    return 1;
}

static int gui_take_menu_event(HVM_GuiState *gui) {
    int cmd;
    if (!gui) return 0;
    cmd = gui->last_menu_cmd;
    gui->last_menu_cmd = 0;
    return cmd;
}

static char *gui_file_dialog(HVM_GuiState *gui, int save_mode) {
    OPENFILENAMEA ofn;
    char path[MAX_PATH];
    BOOL ok;

    if (!gui || !gui->hwnd) return NULL;

    memset(path, 0, sizeof(path));
    memset(&ofn, 0, sizeof(ofn));
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = gui->hwnd;
    ofn.lpstrFile = path;
    ofn.nMaxFile = MAX_PATH;
    ofn.lpstrFilter = "HOSC/Text Files\0*.hosc;*.txt\0All Files\0*.*\0\0";
    ofn.nFilterIndex = 1;
    ofn.Flags = OFN_EXPLORER | OFN_PATHMUSTEXIST | OFN_HIDEREADONLY;

    if (save_mode) {
        ofn.Flags |= OFN_OVERWRITEPROMPT;
        ofn.lpstrDefExt = "txt";
        ok = GetSaveFileNameA(&ofn);
    } else {
        ofn.Flags |= OFN_FILEMUSTEXIST;
        ok = GetOpenFileNameA(&ofn);
    }

    if (!ok || path[0] == '\0') return NULL;
    return strdup(path);
}

static char *gui_open_file_dialog_internal(HVM_GuiState *gui) {
    return gui_file_dialog(gui, 0);
}

static char *gui_save_file_dialog_internal(HVM_GuiState *gui) {
    return gui_file_dialog(gui, 1);
}

static LRESULT CALLBACK hvm_gui_wndproc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam) {
    HVM_GuiState *gui = (HVM_GuiState *)GetWindowLongPtr(hwnd, GWLP_USERDATA);
    switch (msg) {
        case WM_CLOSE:
            DestroyWindow(hwnd);
            return 0;
        case WM_DESTROY:
            if (gui) {
                gui->running = 0;
                gui->hwnd = NULL;
                gui->menu_ready = 0;
                gui->last_menu_cmd = 0;
            }
            PostQuitMessage(0);
            return 0;
        case WM_SIZE:
            if (gui) gui_update_scrollbar(gui);
            return 0;
        case WM_COMMAND:
            if (gui && HIWORD(wparam) == 0) {
                int cmd = (int)LOWORD(wparam);
                if (cmd == HVM_MENU_FILE_NEW || cmd == HVM_MENU_FILE_OPEN || cmd == HVM_MENU_FILE_SAVE || cmd == HVM_MENU_FILE_EXIT) {
                    gui->last_menu_cmd = cmd;
                    if (cmd == HVM_MENU_FILE_EXIT) {
                        DestroyWindow(hwnd);
                    }
                    return 0;
                }
            }
            break;
        case WM_VSCROLL:
            if (gui) gui_handle_vscroll(gui, wparam);
            return 0;
        case WM_MOUSEWHEEL: {
            if (gui) {
                int delta = GET_WHEEL_DELTA_WPARAM(wparam);
                int step = (delta / WHEEL_DELTA) * 40;
                if (step != 0) {
                    gui_scroll_to(gui, gui->scroll_y - step);
                    InvalidateRect(gui->hwnd, NULL, FALSE);
                }
            }
            return 0;
        }
        case WM_MOUSEMOVE:
            if (gui) {
                gui->mouse_x = (int)GET_X_LPARAM(lparam);
                gui->mouse_y = (int)GET_Y_LPARAM(lparam) + gui->scroll_y;
            }
            return 0;
        case WM_LBUTTONDOWN:
            if (gui) gui->mouse_down = 1;
            SetFocus(hwnd);
            return 0;
        case WM_LBUTTONUP:
            if (gui) {
                gui->mouse_down = 0;
                gui->mouse_up = 1;
                gui->mouse_clicked = 1;
                gui->mouse_click_consumed = 0;
                gui->mouse_click_x = (int)GET_X_LPARAM(lparam);
                gui->mouse_click_y = (int)GET_Y_LPARAM(lparam) + gui->scroll_y;
            }
            return 0;
        case WM_KEYDOWN:
            if (gui && (wparam & 0xFF) < 256) {
                int code = (int)(wparam & 0xFF);
                if (!gui->key_down[code]) gui->key_pressed[code] = 1;
                gui->key_down[code] = 1;
            }
            return 0;
        case WM_KEYUP:
            if (gui && (wparam & 0xFF) < 256) gui->key_down[wparam & 0xFF] = 0;
            return 0;
        case WM_KILLFOCUS:
            if (gui) memset(gui->key_down, 0, sizeof(gui->key_down));
            return 0;
        case WM_CHAR:
            if (gui && gui->active_textarea != GUI_NO_ACTIVE_TEXTAREA && gui->active_textarea < gui->textarea_count) {
                HVM_TextAreaState *ta = &gui->textareas[gui->active_textarea];
                if (wparam == 8) {
                    if (ta->len > 0) ta->buffer[--ta->len] = '\0';
                } else if (wparam == 13) {
                    if (!gui_buffer_append_char(&ta->buffer, &ta->len, &ta->cap, '\n')) return 0;
                } else if (wparam == 9) {
                    if (!gui_buffer_append_char(&ta->buffer, &ta->len, &ta->cap, ' ')) return 0;
                    if (!gui_buffer_append_char(&ta->buffer, &ta->len, &ta->cap, ' ')) return 0;
                    if (!gui_buffer_append_char(&ta->buffer, &ta->len, &ta->cap, ' ')) return 0;
                    if (!gui_buffer_append_char(&ta->buffer, &ta->len, &ta->cap, ' ')) return 0;
                } else if (wparam >= 32 && wparam < 127) {
                    if (!gui_buffer_append_char(&ta->buffer, &ta->len, &ta->cap, (char)wparam)) return 0;
                }
                if (gui->hwnd) InvalidateRect(gui->hwnd, NULL, FALSE);
                return 0;
            }

            if (gui && gui->active_input != GUI_NO_ACTIVE_INPUT && gui->active_input < gui->input_count) {
                HVM_InputState *st = &gui->inputs[gui->active_input];
                if (wparam == 8) {
                    if (st->len > 0) st->buffer[--st->len] = '\0';
                } else if (wparam >= 32 && wparam < 127) {
                    if (!gui_buffer_append_char(&st->buffer, &st->len, &st->cap, (char)wparam)) return 0;
                }
                if (gui->hwnd) InvalidateRect(gui->hwnd, NULL, FALSE);
            }
            return 0;
        case WM_ERASEBKGND:
            return 1;
        case WM_PAINT: {
            PAINTSTRUCT ps;
            HDC dc = BeginPaint(hwnd, &ps);
            RECT client;
            HDC mem_dc = NULL;
            HBITMAP mem_bmp = NULL;
            HGDIOBJ old_bmp = NULL;
            HGDIOBJ old_font = NULL;
            size_t i;
            int width;
            int height;
            int y_offset = gui ? gui->scroll_y : 0;

            GetClientRect(hwnd, &client);
            width = client.right - client.left;
            height = client.bottom - client.top;
            if (width <= 0 || height <= 0) {
                EndPaint(hwnd, &ps);
                return 0;
            }

            mem_dc = CreateCompatibleDC(dc);
            if (!mem_dc) {
                EndPaint(hwnd, &ps);
                return 0;
            }

            mem_bmp = CreateCompatibleBitmap(dc, width, height);
            if (!mem_bmp) {
                DeleteDC(mem_dc);
                EndPaint(hwnd, &ps);
                return 0;
            }

            old_bmp = SelectObject(mem_dc, mem_bmp);

            {
                HBRUSH bg = CreateSolidBrush(gui ? gui->bg_color : 0x00FFFFFF);
                FillRect(mem_dc, &client, bg);
                DeleteObject(bg);
            }

            if (gui && gui->font) old_font = SelectObject(mem_dc, gui->font);
            SetBkMode(mem_dc, TRANSPARENT);

            if (gui) {
                for (i = 0; i < gui->cmd_count; i++) {
                    HVM_GuiCmd *cmd = &gui->cmds[i];
                    int draw_y = cmd->y - y_offset;
                    switch (cmd->type) {
                        case GUI_CMD_TEXT: {
                            RECT r;
                            SetTextColor(mem_dc, cmd->color);
                            r.left = cmd->x;
                            r.top = draw_y;
                            r.right = client.right;
                            r.bottom = client.bottom;
                            DrawTextA(mem_dc, cmd->text ? cmd->text : "", -1, &r, DT_LEFT | DT_TOP | DT_NOCLIP);
                            break;
                        }
                        case GUI_CMD_BUTTON: {
                            RECT r = { cmd->x, draw_y, cmd->x + cmd->w, draw_y + cmd->h };
                            HVM_WidgetState *ws = NULL;
                            int hovered = gui_point_in_rect(gui->mouse_x, gui->mouse_y, cmd->x, cmd->y, cmd->w, cmd->h);
                            int down = hovered && gui->mouse_down;
                            if (cmd->payload) {
                                size_t idx = GUI_PAYLOAD_TO_INDEX(cmd->payload);
                                if (idx < gui->widget_count) ws = &gui->widgets[idx];
                            }
                            if (ws) {
                                ws->hovered = hovered;
                                ws->down = down;
                            }
                            {
                                COLORREF fill = RGB(225,225,225);
                                if (down) fill = RGB(190, 210, 245);
                                else if (hovered) fill = RGB(208, 226, 255);
                                {
                                    HBRUSH br = CreateSolidBrush(fill);
                                    FillRect(mem_dc, &r, br);
                                    DeleteObject(br);
                                }
                            }
                            FrameRect(mem_dc, &r, (HBRUSH)GetStockObject(BLACK_BRUSH));
                            SetTextColor(mem_dc, RGB(20, 20, 20));
                            DrawTextA(mem_dc, cmd->text ? cmd->text : "", -1, &r, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
                            break;
                        }
                        case GUI_CMD_INPUT: {
                            RECT r = { cmd->x, draw_y, cmd->x + cmd->w, draw_y + cmd->h };
                            HVM_InputState *st = NULL;
                            const char *text = cmd->text ? cmd->text : "";
                            int placeholder = 1;
                            if (cmd->payload) {
                                size_t idx = GUI_PAYLOAD_TO_INDEX(cmd->payload);
                                if (idx < gui->input_count) st = &gui->inputs[idx];
                            }
                            if (st && st->buffer && st->buffer[0]) {
                                text = st->buffer;
                                placeholder = 0;
                            }
                            {
                                HBRUSH br = CreateSolidBrush(RGB(245,245,245));
                                FillRect(mem_dc, &r, br);
                                DeleteObject(br);
                            }
                            FrameRect(mem_dc, &r, (HBRUSH)GetStockObject(BLACK_BRUSH));
                            SetTextColor(mem_dc, placeholder ? RGB(120, 120, 120) : RGB(20, 20, 20));
                            r.left += 6;
                            DrawTextA(mem_dc, text, -1, &r, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
                            break;
                        }
                        case GUI_CMD_TEXTAREA: {
                            RECT r = { cmd->x, draw_y, cmd->x + cmd->w, draw_y + cmd->h };
                            HBRUSH br = CreateSolidBrush(RGB(255,255,255));
                            FillRect(mem_dc, &r, br);
                            DeleteObject(br);
                            {
                                HPEN pen = CreatePen(PS_SOLID, 2, RGB(30, 30, 30));
                                HGDIOBJ old_pen = SelectObject(mem_dc, pen);
                                HGDIOBJ old_brush = SelectObject(mem_dc, GetStockObject(NULL_BRUSH));
                                Rectangle(mem_dc, r.left, r.top, r.right, r.bottom);
                                SelectObject(mem_dc, old_brush);
                                SelectObject(mem_dc, old_pen);
                                DeleteObject(pen);
                            }
                            {
                                HVM_TextAreaState *ta = NULL;
                                const char *text;
                                RECT tr = r;
                                if (cmd->payload) {
                                    size_t idx = GUI_PAYLOAD_TO_INDEX(cmd->payload);
                                    if (idx < gui->textarea_count) ta = &gui->textareas[idx];
                                }
                                text = ta ? ta->buffer : (cmd->text ? cmd->text : "");
                                tr.left += 6;
                                tr.top += 4;
                                tr.right -= 6;
                                tr.bottom -= 4;
                                if (text && text[0]) {
                                    SetTextColor(mem_dc, RGB(20, 20, 20));
                                    DrawTextA(mem_dc, text, -1, &tr, DT_LEFT | DT_TOP | DT_WORDBREAK | DT_NOPREFIX);
                                } else {
                                    SetTextColor(mem_dc, RGB(130, 130, 130));
                                    DrawTextA(mem_dc, "Type your HOSC code here...", -1, &tr, DT_LEFT | DT_TOP | DT_NOPREFIX);
                                }
                            }
                            break;
                        }
                        case GUI_CMD_IMAGE: {
                            RECT r = { cmd->x, draw_y, cmd->x + cmd->w, draw_y + cmd->h };
                            HBRUSH br = CreateSolidBrush(RGB(200, 220, 240));
                            FillRect(mem_dc, &r, br);
                            DeleteObject(br);
                            FrameRect(mem_dc, &r, (HBRUSH)GetStockObject(BLACK_BRUSH));
                            SetTextColor(mem_dc, RGB(20, 20, 20));
                            DrawTextA(mem_dc, cmd->text ? cmd->text : "[img]", -1, &r, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
                            break;
                        }
                        default:
                            break;
                    }
                }
            }

            BitBlt(dc, 0, 0, width, height, mem_dc, 0, 0, SRCCOPY);

            if (old_font) SelectObject(mem_dc, old_font);
            if (old_bmp) SelectObject(mem_dc, old_bmp);
            DeleteObject(mem_bmp);
            DeleteDC(mem_dc);

            EndPaint(hwnd, &ps);
            return 0;
        }
        default:
            return DefWindowProcA(hwnd, msg, wparam, lparam);
    }

    return DefWindowProcA(hwnd, msg, wparam, lparam);
}

static void gui_pump_events(HVM_GuiState *gui) {
    MSG msg;
    if (!gui) return;
    while (PeekMessageA(&msg, NULL, 0, 0, PM_REMOVE)) {
        if (msg.message == WM_QUIT) {
            gui->running = 0;
            gui->hwnd = NULL;
            continue;
        }
        TranslateMessage(&msg);
        DispatchMessageA(&msg);
    }
}

static void gui_request_repaint(HVM_GuiState *gui) {
    if (!gui || !gui->hwnd) return;
    InvalidateRect(gui->hwnd, NULL, FALSE);
    /* paint is handled by message loop */
}

static int gui_create_window_internal(HVM_GuiState *gui, const char *title) {
    if (!gui) return 0;
    if (gui_headless_mode()) return 0;
    if (!gui_register_class(gui)) return 0;

    while (1) {
        MSG msg;
        if (!PeekMessageA(&msg, NULL, WM_QUIT, WM_QUIT, PM_REMOVE)) break;
    }

    if (gui->hwnd) {
        DestroyWindow(gui->hwnd);
        gui->hwnd = NULL;
    }

    gui_free_commands(gui);
    gui_free_inputs(gui);
    gui_free_textareas(gui);
    gui_free_widgets(gui);
    gui_reset_style_defaults(gui);
    gui->last_tick = (uint64_t)gui_now_ms();
    gui->frame_index = 1;

    gui->hwnd = CreateWindowExA(
        0,
        HVM_GUI_CLASS_NAME,
        title ? title : "HOSC VM Window",
        WS_OVERLAPPEDWINDOW | WS_VSCROLL,
        CW_USEDEFAULT,
        CW_USEDEFAULT,
        900,
        620,
        NULL,
        NULL,
        GetModuleHandleA(NULL),
        NULL
    );

    if (!gui->hwnd) {
        return 0;
    }

    SetWindowLongPtr(gui->hwnd, GWLP_USERDATA, (LONG_PTR)gui);

    ShowWindow(gui->hwnd, SW_SHOW);
    gui_update_scrollbar(gui);
    /* paint is handled by message loop */
    gui->using_real = 1;
    gui->running = 1;
    return 1;
}

static void gui_run_loop_until_close_internal(HVM_GuiState *gui) {
    if (!gui) return;
    while (gui->running && gui->hwnd) {
        gui_reset_transient_input_internal(gui);
        gui_pump_events(gui);
        if (!gui->running || !gui->hwnd) break;

        gui_apply_click_focus_from_commands(gui);
        gui_request_repaint(gui);
        Sleep(16);
    }
}
#else
static int gui_headless_mode(void) { return 1; }
static void gui_update_scrollbar(HVM_GuiState *gui) { (void)gui; }
static int gui_menu_setup_notepad_internal(HVM_GuiState *gui) { (void)gui; return 0; }
static int gui_take_menu_event(HVM_GuiState *gui) { (void)gui; return 0; }
static char *gui_open_file_dialog_internal(HVM_GuiState *gui) { (void)gui; return NULL; }
static char *gui_save_file_dialog_internal(HVM_GuiState *gui) { (void)gui; return NULL; }
static void gui_pump_events(HVM_GuiState *gui) { (void)gui; }
static void gui_request_repaint(HVM_GuiState *gui) { (void)gui; }
static int gui_create_window_internal(HVM_GuiState *gui, const char *title) { (void)gui; (void)title; return 0; }
static void gui_run_loop_until_close_internal(HVM_GuiState *gui) { (void)gui; }
#endif
static void gui_shutdown_internal(HVM_GuiState *gui) {
#ifdef _WIN32
    if (gui && gui->hwnd) {
        DestroyWindow(gui->hwnd);
        gui->hwnd = NULL;
    }
#endif
    if (!gui) return;
    gui_free_commands(gui);
    gui_free_inputs(gui);
    gui_free_textareas(gui);
    gui_free_widgets(gui);
    gui_reset_style_defaults(gui);
    gui->last_tick = (uint64_t)gui_now_ms();
    gui->frame_index = 1;
    gui->using_real = 0;
    gui->running = 0;
    gui->loop_called = 0;
}

static void gui_prepare_fallback(HVM_GuiState *gui, const char *title) {
    if (!gui) return;
    gui->using_real = 0;
    gui->running = 1;
    gui_free_commands(gui);
    gui_free_inputs(gui);
    gui_free_textareas(gui);
    gui_free_widgets(gui);
    gui_reset_style_defaults(gui);
    gui->last_tick = (uint64_t)gui_now_ms();
    gui->frame_index = 1;
    GUI_LOG(gui, "[GUI] window (console fallback): %s\n", title ? title : "HOSC VM Window");
}

static int runtime_gui_create_window(HVM_RuntimeServices *services, const char *title) {
    HVM_GuiState *gui = gui_from_services(services);
    int success;
    if (!gui) return 0;

    success = gui_create_window_internal(gui, title);
    if (success) {
        GUI_LOG(gui, "[GUI] real window: %s\n", title ? title : "HOSC VM Window");
        return 1;
    }

    gui_prepare_fallback(gui, title);
    return 0;
}

static void runtime_gui_shutdown(HVM_RuntimeServices *services) {
    gui_shutdown_internal(gui_from_services(services));
}

static void runtime_gui_prepare_run(HVM_RuntimeServices *services) {
    HVM_GuiState *gui = gui_from_services(services);
    if (!gui) return;
    gui->loop_called = 0;
    if (gui->frame_index == 0) gui->frame_index = 1;
    gui_reset_transient_input_internal(gui);
}

static void runtime_gui_finish_run(HVM_RuntimeServices *services) {
    HVM_GuiState *gui = gui_from_services(services);
    if (!gui) return;
    if (!gui->using_real || !gui->running) return;
    if (!gui->loop_called) {
        GUI_LOG(gui, "[GUI] Close the window to exit.\n");
    }
    gui_run_loop_until_close_internal(gui);
}

static void runtime_gui_clear_commands(HVM_RuntimeServices *services) {
    gui_clear_commands_internal(gui_from_services(services));
}

static void runtime_gui_set_bg_color(HVM_RuntimeServices *services, unsigned int color) {
    HVM_GuiState *gui = gui_from_services(services);
    if (!gui) return;
    gui->bg_color = color;
}

static void runtime_gui_set_fg_color(HVM_RuntimeServices *services, unsigned int color) {
    HVM_GuiState *gui = gui_from_services(services);
    if (!gui) return;
    gui->fg_color = color;
}

static void runtime_gui_set_font_size(HVM_RuntimeServices *services, int size) {
    HVM_GuiState *gui = gui_from_services(services);
    if (!gui) return;
    if (size < 8) size = 8;
    if (size > 96) size = 96;
    gui->font_size = size;
#ifdef _WIN32
    if (gui->font) { DeleteObject(gui->font); gui->font = NULL; }
    gui->font = CreateFontA(gui->font_size, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, ANSI_CHARSET,
                             OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, DEFAULT_QUALITY,
                             DEFAULT_PITCH | FF_DONTCARE, "Segoe UI");
#endif
}

static int runtime_gui_draw_text(HVM_RuntimeServices *services, int x, int y, const char *text) {
    HVM_GuiState *gui = gui_from_services(services);
    HVM_GuiCmd cmd;
    if (!gui) return 0;
    memset(&cmd, 0, sizeof(cmd));
    cmd.type = GUI_CMD_TEXT;
    cmd.x = x;
    cmd.y = y;
    cmd.color = gui->fg_color;
    cmd.text = (char *)(text ? text : "");
    if (gui->using_real && gui->running) {
        if (!gui_push_cmd(gui, &cmd)) return 0;
    } else if (!gui->using_real) {
        GUI_LOG(gui, "[GUI] text (%d,%d): %s\n", x, y, text ? text : "");
    }
    return 1;
}

static int runtime_gui_draw_button(HVM_RuntimeServices *services, int x, int y, int w, int h, const char *label, int *clicked) {
    HVM_GuiState *gui = gui_from_services(services);
    HVM_GuiCmd cmd;
    HVM_WidgetState *ws;
    size_t widx = (size_t)(-1);
    int was_clicked = 0;
    const char *button_id = label ? label : "";
    if (!gui) return 0;

    ws = gui_register_widget_rect(gui, button_id, x, y, w, h);
    if (ws) widx = (size_t)(ws - gui->widgets);
    was_clicked = gui_take_mouse_click_in_rect(gui, x, y, w, h);
    if (ws) ws->clicked = was_clicked;

    memset(&cmd, 0, sizeof(cmd));
    cmd.type = GUI_CMD_BUTTON;
    cmd.x = x;
    cmd.y = y;
    cmd.w = w;
    cmd.h = h;
    cmd.text = (char *)button_id;
    cmd.payload = (ws && widx != (size_t)(-1)) ? GUI_PAYLOAD_INDEX(widx) : NULL;

    if (gui->using_real && gui->running) {
        if (!gui_push_cmd(gui, &cmd)) return 0;
    } else if (!gui->using_real) {
        GUI_LOG(gui, "[GUI] button (%d,%d,%d,%d): %s\n", x, y, w, h, button_id);
    }

    if (clicked) *clicked = was_clicked;
    return 1;
}

static int runtime_gui_draw_button_state(HVM_RuntimeServices *services, const char *id, const char *label, int *clicked) {
    HVM_GuiState *gui = gui_from_services(services);
    HVM_GuiCmd cmd;
    HVM_WidgetState *ws;
    size_t widx = (size_t)(-1);
    int bx, by, bw, bh;
    int was_clicked = 0;
    const char *safe_id = id ? id : "";
    const char *safe_label = label ? label : "Button";

    if (!gui) return 0;

    gui_layout_place_widget(gui, 180, 34, &bx, &by, &bw, &bh);
    ws = gui_register_widget_rect(gui, safe_id, bx, by, bw, bh);
    if (ws) widx = (size_t)(ws - gui->widgets);
    was_clicked = gui_take_mouse_click_in_rect(gui, bx, by, bw, bh);
    if (ws) ws->clicked = was_clicked;

    memset(&cmd, 0, sizeof(cmd));
    cmd.type = GUI_CMD_BUTTON;
    cmd.x = bx;
    cmd.y = by;
    cmd.w = bw;
    cmd.h = bh;
    cmd.text = (char *)safe_label;
    cmd.payload = (ws && widx != (size_t)(-1)) ? GUI_PAYLOAD_INDEX(widx) : NULL;

    if (gui->using_real && gui->running) {
        if (!gui_push_cmd(gui, &cmd)) return 0;
    } else if (!gui->using_real) {
        GUI_LOG(gui, "[GUI] button(id=%s) (%d,%d,%d,%d): %s\n", safe_id, bx, by, bw, bh, safe_label);
    }

    if (clicked) *clicked = was_clicked;
    return 1;
}

static int runtime_gui_draw_input(HVM_RuntimeServices *services, int x, int y, int w, const char *label, const char **out_text) {
    HVM_GuiState *gui = gui_from_services(services);
    HVM_GuiCmd cmd;
    HVM_InputState *st;
    size_t idx;
    const char *safe_label = label ? label : "";

    if (!gui) return 0;

    st = gui_get_input_state(gui, safe_label);
    idx = st ? (size_t)(st - gui->inputs) : GUI_NO_ACTIVE_INPUT;

    memset(&cmd, 0, sizeof(cmd));
    cmd.type = GUI_CMD_INPUT;
    cmd.x = x;
    cmd.y = y;
    cmd.w = w;
    cmd.h = 28;
    cmd.text = (char *)safe_label;
    cmd.payload = (idx != GUI_NO_ACTIVE_INPUT) ? GUI_PAYLOAD_INDEX(idx) : NULL;

    if (gui_take_mouse_focus_in_rect(gui, cmd.x, cmd.y, cmd.w, cmd.h)) {
        gui->active_input = idx;
        gui->active_textarea = GUI_NO_ACTIVE_TEXTAREA;
    }

    if (gui->using_real && gui->running) {
        if (!gui_push_cmd(gui, &cmd)) return 0;
    } else if (!gui->using_real) {
        GUI_LOG(gui, "[GUI] input (%d,%d,%d): %s\n", x, y, w, safe_label);
    }

    if (out_text) *out_text = (st && st->buffer) ? st->buffer : "";
    return 1;
}

static int runtime_gui_draw_input_state(HVM_RuntimeServices *services, const char *id, const char *placeholder, const char **out_text) {
    HVM_GuiState *gui = gui_from_services(services);
    HVM_GuiCmd cmd;
    HVM_InputState *st;
    size_t idx;
    int ix, iy, iw, ih;
    const char *safe_id = id ? id : "";
    const char *safe_placeholder = placeholder ? placeholder : "";

    if (!gui) return 0;

    st = gui_get_input_state(gui, safe_id);
    idx = st ? (size_t)(st - gui->inputs) : GUI_NO_ACTIVE_INPUT;
    if (st && st->len == 0 && safe_placeholder[0] != '\0') {
        gui_buffer_set(&st->buffer, &st->len, &st->cap, safe_placeholder);
    }

    gui_layout_place_widget(gui, 260, 30, &ix, &iy, &iw, &ih);
    gui_register_widget_rect(gui, safe_id, ix, iy, iw, ih);

    memset(&cmd, 0, sizeof(cmd));
    cmd.type = GUI_CMD_INPUT;
    cmd.x = ix;
    cmd.y = iy;
    cmd.w = iw;
    cmd.h = ih;
    cmd.text = (char *)safe_placeholder;
    cmd.payload = (idx != GUI_NO_ACTIVE_INPUT) ? GUI_PAYLOAD_INDEX(idx) : NULL;

    if (gui_take_mouse_focus_in_rect(gui, cmd.x, cmd.y, cmd.w, cmd.h)) {
        gui->active_input = idx;
        gui->active_textarea = GUI_NO_ACTIVE_TEXTAREA;
    }

    if (gui->using_real && gui->running) {
        if (!gui_push_cmd(gui, &cmd)) return 0;
    } else if (!gui->using_real) {
        GUI_LOG(gui, "[GUI] input(id=%s) (%d,%d,%d): %s\n", safe_id, ix, iy, iw, safe_placeholder);
    }

    if (out_text) *out_text = (st && st->buffer) ? st->buffer : "";
    return 1;
}

static int runtime_gui_draw_textarea(HVM_RuntimeServices *services, int x, int y, int w, int h, const char *id, const char **out_text) {
    HVM_GuiState *gui = gui_from_services(services);
    HVM_GuiCmd cmd;
    HVM_TextAreaState *ta;
    size_t idx;
    int height = h < 40 ? 40 : h;
    const char *safe_id = id ? id : "";

    if (!gui) return 0;

    ta = gui_get_textarea_state(gui, safe_id);
    idx = ta ? (size_t)(ta - gui->textareas) : GUI_NO_ACTIVE_TEXTAREA;

    memset(&cmd, 0, sizeof(cmd));
    cmd.type = GUI_CMD_TEXTAREA;
    cmd.x = x;
    cmd.y = y;
    cmd.w = w;
    cmd.h = height;
    cmd.text = (char *)safe_id;
    cmd.payload = (idx != GUI_NO_ACTIVE_TEXTAREA) ? GUI_PAYLOAD_INDEX(idx) : NULL;

    if (gui_take_mouse_focus_in_rect(gui, cmd.x, cmd.y, cmd.w, cmd.h)) {
        gui->active_textarea = idx;
        gui->active_input = GUI_NO_ACTIVE_INPUT;
    }

    if (gui->using_real && gui->running) {
        if (!gui_push_cmd(gui, &cmd)) return 0;
    } else if (!gui->using_real) {
        GUI_LOG(gui, "[GUI] textarea (%d,%d,%d,%d): %s\n", x, y, w, height, safe_id);
    }

    if (out_text) *out_text = (ta && ta->buffer) ? ta->buffer : "";
    return 1;
}

static int runtime_gui_draw_image(HVM_RuntimeServices *services, int x, int y, int w, int h, const char *label) {
    HVM_GuiState *gui = gui_from_services(services);
    HVM_GuiCmd cmd;
    const char *safe_label = label ? label : "[img]";
    if (!gui) return 0;

    memset(&cmd, 0, sizeof(cmd));
    cmd.type = GUI_CMD_IMAGE;
    cmd.x = x;
    cmd.y = y;
    cmd.w = w;
    cmd.h = h;
    cmd.text = (char *)safe_label;

    if (gui->using_real && gui->running) {
        if (!gui_push_cmd(gui, &cmd)) return 0;
    } else if (!gui->using_real) {
        GUI_LOG(gui, "[GUI] image (%d,%d,%d,%d): %s\n", x, y, w, h, safe_label);
    }
    return 1;
}

static int runtime_gui_get_mouse_x(HVM_RuntimeServices *services) {
    HVM_GuiState *gui = gui_from_services(services);
    return gui ? gui->mouse_x : 0;
}

static int runtime_gui_get_mouse_y(HVM_RuntimeServices *services) {
    HVM_GuiState *gui = gui_from_services(services);
    return gui ? gui->mouse_y : 0;
}

static int runtime_gui_is_mouse_down(HVM_RuntimeServices *services) {
    HVM_GuiState *gui = gui_from_services(services);
    return gui ? gui->mouse_down : 0;
}

static int runtime_gui_was_mouse_up(HVM_RuntimeServices *services) {
    HVM_GuiState *gui = gui_from_services(services);
    int val = gui ? gui->mouse_up : 0;
    if (gui) gui->mouse_up = 0;
    return val;
}

static int runtime_gui_was_mouse_click(HVM_RuntimeServices *services) {
    HVM_GuiState *gui = gui_from_services(services);
    int val = gui ? gui->mouse_clicked : 0;
    if (gui) gui->mouse_clicked = 0;
    return val;
}

static int runtime_gui_is_mouse_hover(HVM_RuntimeServices *services, const char *id) {
    HVM_GuiState *gui = gui_from_services(services);
    HVM_WidgetState *ws;
    int hovered = 0;
    if (!gui) return 0;
    ws = gui_find_widget_state(gui, id ? id : "");
    if (ws && ws->frame_seen == gui->frame_index) {
        hovered = gui_point_in_rect(gui->mouse_x, gui->mouse_y, ws->x, ws->y, ws->w, ws->h);
    }
    return hovered;
}

static int runtime_gui_is_key_down(HVM_RuntimeServices *services, int key) {
    HVM_GuiState *gui = gui_from_services(services);
    if (!gui) return 0;
    if (key < 0 || key >= 256) key = 0;
    return gui->key_down[key & 0xFF];
}

static int runtime_gui_was_key_press(HVM_RuntimeServices *services, int key) {
    HVM_GuiState *gui = gui_from_services(services);
    int pressed;
    if (!gui) return 0;
    if (key < 0 || key >= 256) key = 0;
    pressed = gui->key_pressed[key & 0xFF];
    gui->key_pressed[key & 0xFF] = 0;
    return pressed;
}

static double runtime_gui_get_delta_ms(HVM_RuntimeServices *services) {
    HVM_GuiState *gui = gui_from_services(services);
    return gui ? gui->delta_ms : 0.0;
}

static int runtime_gui_layout_reset(HVM_RuntimeServices *services, int x, int y, int gap) {
    HVM_GuiState *gui = gui_from_services(services);
    if (!gui) return 0;
    gui->layout_x = x;
    gui->layout_y = y;
    gui->layout_base_x = x;
    gui->layout_base_y = y;
    gui->layout_gap = gap < 0 ? 0 : gap;

    gui->layout_mode = GUI_LAYOUT_FLOW;
    gui->layout_index = 0;
    return 1;
}

static int runtime_gui_layout_next(HVM_RuntimeServices *services, int height) {
    HVM_GuiState *gui = gui_from_services(services);
    int y;
    if (!gui) return 0;
    if (height < 0) height = 0;

    switch ((HVM_GuiLayoutMode)gui->layout_mode) {
        case GUI_LAYOUT_ROW_MODE: {
            int cols = gui->layout_cols > 0 ? gui->layout_cols : 1;
            int row_h = gui->layout_row_height > 0 ? gui->layout_row_height : height;
            int next_row;
            gui->layout_index = ((gui->layout_index / cols) + 1) * cols;
            next_row = gui->layout_index / cols;
            gui->layout_y = gui->layout_base_y + next_row * (row_h + gui->layout_gap);
            break;
        }
        case GUI_LAYOUT_GRID_MODE: {
            int cols = gui->layout_cols > 0 ? gui->layout_cols : 2;
            int cell_h = gui->layout_grid_cell_h > 0 ? gui->layout_grid_cell_h : height;
            int next_row;
            gui->layout_index = ((gui->layout_index / cols) + 1) * cols;
            next_row = gui->layout_index / cols;
            gui->layout_y = gui->layout_base_y + next_row * (cell_h + gui->layout_gap);
            break;
        }
        case GUI_LAYOUT_COLUMN_MODE:
        case GUI_LAYOUT_FLOW:
        default:
            gui->layout_y += height + gui->layout_gap;
            gui->layout_index++;
            break;
    }

    y = gui->layout_y;
    return y;
}

static int runtime_gui_layout_row(HVM_RuntimeServices *services, int cols) {
    HVM_GuiState *gui = gui_from_services(services);
    if (!gui) return 0;
    if (cols < 1) cols = 1;
    gui->layout_mode = GUI_LAYOUT_ROW_MODE;
    gui->layout_cols = cols;
    if (gui->layout_col_width < 1) gui->layout_col_width = 180;
    if (gui->layout_row_height < 1) gui->layout_row_height = 32;
    gui->layout_base_y = gui->layout_y;
    gui->layout_x = gui->layout_base_x;
    gui->layout_index = 0;
    return cols;
}

static int runtime_gui_layout_column(HVM_RuntimeServices *services, int width) {
    HVM_GuiState *gui = gui_from_services(services);
    if (!gui) return 0;
    if (width < 40) width = 40;
    gui->layout_mode = GUI_LAYOUT_COLUMN_MODE;
    gui->layout_col_width = width;
    gui->layout_base_y = gui->layout_y;
    gui->layout_x = gui->layout_base_x;
    gui->layout_index = 0;
    return width;
}

static int runtime_gui_layout_grid(HVM_RuntimeServices *services, int cols, int cell_w, int cell_h) {
    HVM_GuiState *gui = gui_from_services(services);
    if (!gui) return 0;
    if (cols < 1) cols = 1;
    if (cell_w < 40) cell_w = 40;
    if (cell_h < 20) cell_h = 20;
    gui->layout_mode = GUI_LAYOUT_GRID_MODE;
    gui->layout_cols = cols;
    gui->layout_grid_cell_w = cell_w;
    gui->layout_grid_cell_h = cell_h;
    gui->layout_base_y = gui->layout_y;
    gui->layout_x = gui->layout_base_x;
    gui->layout_index = 0;
    return cols;
}

static int runtime_gui_loop_tick(HVM_RuntimeServices *services, double *delta_ms) {
    HVM_GuiState *gui = gui_from_services(services);
    double now;
    if (!gui) return 0;
    now = gui_now_ms();
    gui->delta_ms = (gui->last_tick == 0) ? 0.0 : (now - (double)gui->last_tick);
    gui->last_tick = (uint64_t)now;
    gui->loop_called = 1;

    gui_reset_transient_input_internal(gui);

    if (gui->using_real && !gui->running) {
        if (delta_ms) *delta_ms = gui->delta_ms;
        return 0;
    }

    if (gui->using_real && gui->running) {
        gui_pump_events(gui);
        gui_apply_click_focus_from_commands(gui);
        gui_request_repaint(gui);
        gui->layout_x = gui->layout_base_x;
        gui->layout_y = gui->layout_base_y;
        gui->layout_index = 0;
        if (gui->frame_index == 0) gui->frame_index = 1;
        else gui->frame_index++;
    } else if (!gui->using_real) {
        if (gui->frame_index == 0) gui->frame_index = 1;
        else gui->frame_index++;
        GUI_LOG(gui, "[GUI] loop tick\n");
    }

    if (delta_ms) *delta_ms = gui->delta_ms;
    return 1;
}

static int runtime_gui_menu_setup_notepad(HVM_RuntimeServices *services) {
    HVM_GuiState *gui = gui_from_services(services);
    if (!gui || !gui->using_real || !gui->running) return 0;
    return gui_menu_setup_notepad_internal(gui);
}

static int runtime_gui_menu_event(HVM_RuntimeServices *services) {
    HVM_GuiState *gui = gui_from_services(services);
    if (gui && gui->using_real && gui->running) gui_pump_events(gui);
    return gui_take_menu_event(gui);
}

static int runtime_gui_scroll_set_range(HVM_RuntimeServices *services, int range) {
    HVM_GuiState *gui = gui_from_services(services);
    if (!gui) return 0;
    if (range < 0) range = 0;
    gui->scroll_range = range;
#ifdef _WIN32
    if (gui->using_real && gui->running) gui_update_scrollbar(gui);
#endif
    return gui->scroll_range;
}

static int runtime_gui_scroll_y(HVM_RuntimeServices *services) {
    HVM_GuiState *gui = gui_from_services(services);
    return gui ? gui->scroll_y : 0;
}

static char *runtime_gui_open_file_dialog(HVM_RuntimeServices *services) {
    HVM_GuiState *gui = gui_from_services(services);
    if (!gui || !gui->using_real || !gui->running) return NULL;
    return gui_open_file_dialog_internal(gui);
}

static char *runtime_gui_save_file_dialog(HVM_RuntimeServices *services) {
    HVM_GuiState *gui = gui_from_services(services);
    if (!gui || !gui->using_real || !gui->running) return NULL;
    return gui_save_file_dialog_internal(gui);
}

static int runtime_gui_input_set(HVM_RuntimeServices *services, const char *label, const char *text) {
    HVM_GuiState *gui = gui_from_services(services);
    HVM_InputState *st;
    if (!gui) return 0;
    st = gui_get_input_state(gui, label ? label : "");
    if (!st) return 0;
    return gui_buffer_set(&st->buffer, &st->len, &st->cap, text ? text : "");
}

static int runtime_gui_textarea_set(HVM_RuntimeServices *services, const char *id, const char *text) {
    HVM_GuiState *gui = gui_from_services(services);
    HVM_TextAreaState *ta;
    if (!gui) return 0;
    ta = gui_get_textarea_state(gui, id ? id : "");
    if (!ta) return 0;
    return gui_buffer_set(&ta->buffer, &ta->len, &ta->cap, text ? text : "");
}

HVM_GuiState *hvm_gui_create_state(void) {
    HVM_GuiState *gui = (HVM_GuiState *)calloc(1, sizeof(HVM_GuiState));
    if (!gui) return NULL;
    gui->active_input = GUI_NO_ACTIVE_INPUT;
    gui->active_textarea = GUI_NO_ACTIVE_TEXTAREA;
    gui->frame_index = 1;
    gui_reset_style_defaults(gui);
    return gui;
}

void hvm_gui_destroy_state(HVM_GuiState *state) {
    if (!state) return;
    gui_shutdown_internal(state);
    free(state);
}

void hvm_gui_bind_services(HVM_RuntimeServices *services, HVM_GuiState *state) {
    if (!services) return;
    services->context = state;
    services->gui.create_window = runtime_gui_create_window;
    services->gui.shutdown = runtime_gui_shutdown;
    services->gui.prepare_run = runtime_gui_prepare_run;
    services->gui.finish_run = runtime_gui_finish_run;
    services->gui.clear_commands = runtime_gui_clear_commands;
    services->gui.set_bg_color = runtime_gui_set_bg_color;
    services->gui.set_fg_color = runtime_gui_set_fg_color;
    services->gui.set_font_size = runtime_gui_set_font_size;
    services->gui.draw_text = runtime_gui_draw_text;
    services->gui.draw_button = runtime_gui_draw_button;
    services->gui.draw_button_state = runtime_gui_draw_button_state;
    services->gui.draw_input = runtime_gui_draw_input;
    services->gui.draw_input_state = runtime_gui_draw_input_state;
    services->gui.draw_textarea = runtime_gui_draw_textarea;
    services->gui.draw_image = runtime_gui_draw_image;
    services->gui.get_mouse_x = runtime_gui_get_mouse_x;
    services->gui.get_mouse_y = runtime_gui_get_mouse_y;
    services->gui.is_mouse_down = runtime_gui_is_mouse_down;
    services->gui.was_mouse_up = runtime_gui_was_mouse_up;
    services->gui.was_mouse_click = runtime_gui_was_mouse_click;
    services->gui.is_mouse_hover = runtime_gui_is_mouse_hover;
    services->gui.is_key_down = runtime_gui_is_key_down;
    services->gui.was_key_press = runtime_gui_was_key_press;
    services->gui.get_delta_ms = runtime_gui_get_delta_ms;
    services->gui.layout_reset = runtime_gui_layout_reset;
    services->gui.layout_next = runtime_gui_layout_next;
    services->gui.layout_row = runtime_gui_layout_row;
    services->gui.layout_column = runtime_gui_layout_column;
    services->gui.layout_grid = runtime_gui_layout_grid;
    services->gui.loop_tick = runtime_gui_loop_tick;
    services->gui.menu_setup_notepad = runtime_gui_menu_setup_notepad;
    services->gui.menu_event = runtime_gui_menu_event;
    services->gui.scroll_set_range = runtime_gui_scroll_set_range;
    services->gui.scroll_y = runtime_gui_scroll_y;
    services->gui.open_file_dialog = runtime_gui_open_file_dialog;
    services->gui.save_file_dialog = runtime_gui_save_file_dialog;
    services->gui.input_set = runtime_gui_input_set;
    services->gui.textarea_set = runtime_gui_textarea_set;
}
