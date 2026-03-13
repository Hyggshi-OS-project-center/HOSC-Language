/* runtime_gui.c - GUI backend for HOSC VM runtime */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <stdint.h>

#include "hvm.h"
#include "runtime_gui.h"
#include "hvm_internal.h"

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

#define g_gui (*gui)
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

typedef struct HVM_GuiState {
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

    int class_registered;

    int using_real;
    int running;
    int loop_called;
} HVM_GuiState;

static int gui_debug_enabled(void) {
    const char *env = getenv("HVM_GUI_DEBUG");
    if (!env || env[0] == '\0') return 0;
    if (strcmp(env, "1") == 0 || strcmp(env, "true") == 0 || strcmp(env, "yes") == 0) return 1;
    return 0;
}

#define GUI_LOG(...) do { if (!g_gui.using_real || gui_debug_enabled()) printf(__VA_ARGS__); } while (0)

static void gui_free_commands(HVM_GuiRuntime *gui) {
    size_t i;
    for (i = 0; i < g_gui.cmd_count; i++) {
        free(g_gui.cmds[i].text);
        g_gui.cmds[i].text = NULL;
    }
    free(g_gui.cmds);
    g_gui.cmds = NULL;
    g_gui.cmd_count = 0;
    g_gui.cmd_cap = 0;
}

static void gui_clear_commands(HVM_GuiRuntime *gui) {
    size_t i;
    for (i = 0; i < g_gui.cmd_count; i++) {
        free(g_gui.cmds[i].text);
        g_gui.cmds[i].text = NULL;
    }
    g_gui.cmd_count = 0;
}

static int gui_buffer_set(HVM_GuiRuntime *gui, char **buffer, size_t *len, size_t *cap, const char *value) {
    size_t n;
    char *nb;
    (void)gui;
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

static int gui_buffer_append_char(HVM_GuiRuntime *gui, char **buffer, size_t *len, size_t *cap, char ch) {
    char *nb;
    size_t new_cap;
    (void)gui;
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

static void gui_free_inputs(HVM_GuiRuntime *gui) {
    size_t i;
    for (i = 0; i < g_gui.input_count; i++) {
        free(g_gui.inputs[i].label);
        free(g_gui.inputs[i].buffer);
    }
    free(g_gui.inputs);
    g_gui.inputs = NULL;
    g_gui.input_count = 0;
    g_gui.input_cap = 0;
    g_gui.active_input = GUI_NO_ACTIVE_INPUT;
}

static void gui_free_textareas(HVM_GuiRuntime *gui) {
    size_t i;
    for (i = 0; i < g_gui.textarea_count; i++) {
        free(g_gui.textareas[i].id);
        free(g_gui.textareas[i].buffer);
    }
    free(g_gui.textareas);
    g_gui.textareas = NULL;
    g_gui.textarea_count = 0;
    g_gui.textarea_cap = 0;
    g_gui.active_textarea = GUI_NO_ACTIVE_TEXTAREA;
}

static void gui_free_widgets(HVM_GuiRuntime *gui) {
    size_t i;
    for (i = 0; i < g_gui.widget_count; i++) {
        free(g_gui.widgets[i].id);
        g_gui.widgets[i].id = NULL;
    }
    free(g_gui.widgets);
    g_gui.widgets = NULL;
    g_gui.widget_count = 0;
    g_gui.widget_cap = 0;
}

static HVM_WidgetState* gui_find_widget_state(HVM_GuiRuntime *gui, const char *id) {
    size_t i;
    if (!id) id = "";
    for (i = 0; i < g_gui.widget_count; i++) {
        if (g_gui.widgets[i].id && strcmp(g_gui.widgets[i].id, id) == 0) {
            return &g_gui.widgets[i];
        }
    }
    return NULL;
}

static HVM_WidgetState* gui_get_widget_state(HVM_GuiRuntime *gui, const char *id) {
    HVM_WidgetState *state;
    if (!id) id = "";

    state = gui_find_widget_state(gui, id);
    if (state) return state;

    if (g_gui.widget_count == g_gui.widget_cap) {
        size_t new_cap = g_gui.widget_cap == 0 ? 16 : g_gui.widget_cap * 2;
        HVM_WidgetState *nw = (HVM_WidgetState *)realloc(g_gui.widgets, new_cap * sizeof(HVM_WidgetState));
        if (!nw) return NULL;
        memset(nw + g_gui.widget_cap, 0, (new_cap - g_gui.widget_cap) * sizeof(HVM_WidgetState));
        g_gui.widgets = nw;
        g_gui.widget_cap = new_cap;
    }

    g_gui.widgets[g_gui.widget_count].id = strdup(id);
    if (!g_gui.widgets[g_gui.widget_count].id) return NULL;

    g_gui.widgets[g_gui.widget_count].x = 0;
    g_gui.widgets[g_gui.widget_count].y = 0;
    g_gui.widgets[g_gui.widget_count].w = 0;
    g_gui.widgets[g_gui.widget_count].h = 0;
    g_gui.widgets[g_gui.widget_count].hovered = 0;
    g_gui.widgets[g_gui.widget_count].down = 0;
    g_gui.widgets[g_gui.widget_count].clicked = 0;
    g_gui.widgets[g_gui.widget_count].frame_seen = 0;

    g_gui.widget_count++;
    return &g_gui.widgets[g_gui.widget_count - 1];
}

static int gui_point_in_rect(HVM_GuiRuntime *gui, int px, int py, int x, int y, int w, int h) {
    (void)gui;
    if (w < 0) w = 0;
    if (h < 0) h = 0;
    return px >= x && py >= y && px <= x + w && py <= y + h;
}

static int gui_take_mouse_click_in_rect(HVM_GuiRuntime *gui, int x, int y, int w, int h) {
    if (!g_gui.mouse_clicked || g_gui.mouse_click_consumed) return 0;
    if (!gui_point_in_rect(gui, g_gui.mouse_click_x, g_gui.mouse_click_y, x, y, w, h)) return 0;
    g_gui.mouse_click_consumed = 1;
    return 1;
}

static int gui_take_mouse_focus_in_rect(HVM_GuiRuntime *gui, int x, int y, int w, int h) {
    if (gui_take_mouse_click_in_rect(gui, x, y, w, h)) return 1;
    if (g_gui.mouse_down && gui_point_in_rect(gui, g_gui.mouse_x, g_gui.mouse_y, x, y, w, h)) {
        return 1;
    }
    return 0;
}

static void gui_reset_transient_input(HVM_GuiRuntime *gui) {
    g_gui.mouse_up = 0;
    g_gui.mouse_clicked = 0;
    g_gui.mouse_click_consumed = 0;
    memset(g_gui.key_pressed, 0, sizeof(g_gui.key_pressed));
}

static void gui_apply_click_focus_from_commands(HVM_GuiRuntime *gui) {
    size_t i;

    if (!g_gui.mouse_clicked || g_gui.mouse_click_consumed) {
        return;
    }

    for (i = g_gui.cmd_count; i > 0; i--) {
        HVM_GuiCmd *cmd = &g_gui.cmds[i - 1];

        if (!gui_point_in_rect(gui, g_gui.mouse_click_x, g_gui.mouse_click_y, cmd->x, cmd->y, cmd->w, cmd->h)) {
            continue;
        }

        if (cmd->type == GUI_CMD_INPUT) {
            size_t idx = GUI_PAYLOAD_TO_INDEX(cmd->payload);
            if (idx < g_gui.input_count) {
                g_gui.active_input = idx;
                g_gui.active_textarea = GUI_NO_ACTIVE_TEXTAREA;
                g_gui.mouse_click_consumed = 1;
                return;
            }
        }

        if (cmd->type == GUI_CMD_TEXTAREA) {
            size_t idx = GUI_PAYLOAD_TO_INDEX(cmd->payload);
            if (idx < g_gui.textarea_count) {
                g_gui.active_textarea = idx;
                g_gui.active_input = GUI_NO_ACTIVE_INPUT;
                g_gui.mouse_click_consumed = 1;
                return;
            }
        }
    }
}
static HVM_WidgetState* gui_register_widget_rect(HVM_GuiRuntime *gui, const char *id, int x, int y, int w, int h) {
    HVM_WidgetState *state = gui_get_widget_state(gui, id);
    if (!state) return NULL;

    state->x = x;
    state->y = y;
    state->w = w;
    state->h = h;
    state->hovered = gui_point_in_rect(gui, g_gui.mouse_x, g_gui.mouse_y, x, y, w, h);
    state->down = state->hovered && g_gui.mouse_down;
    state->clicked = gui_point_in_rect(gui, g_gui.mouse_click_x, g_gui.mouse_click_y, x, y, w, h) && g_gui.mouse_clicked;
    state->frame_seen = g_gui.frame_index;
    return state;
}

static void gui_layout_place_widget(HVM_GuiRuntime *gui, int pref_w, int pref_h, int *x, int *y, int *w, int *h) {
    int px;
    int py;
    int pw;
    int ph;

    if (pref_w < 1) pref_w = 160;
    if (pref_h < 1) pref_h = 32;

    px = g_gui.layout_x;
    py = g_gui.layout_y;
    pw = pref_w;
    ph = pref_h;

    switch ((HVM_GuiLayoutMode)g_gui.layout_mode) {
        case GUI_LAYOUT_ROW_MODE: {
            int cols = g_gui.layout_cols > 0 ? g_gui.layout_cols : 1;
            int col = g_gui.layout_index % cols;
            int row = g_gui.layout_index / cols;
            int cell_w = g_gui.layout_col_width > 0 ? g_gui.layout_col_width : pref_w;
            int cell_h = g_gui.layout_row_height > 0 ? g_gui.layout_row_height : pref_h;
            px = g_gui.layout_base_x + col * (cell_w + g_gui.layout_gap);
            py = g_gui.layout_base_y + row * (cell_h + g_gui.layout_gap);
            pw = cell_w;
            ph = cell_h;
            g_gui.layout_index++;
            break;
        }
        case GUI_LAYOUT_COLUMN_MODE: {
            int cell_w = g_gui.layout_col_width > 0 ? g_gui.layout_col_width : pref_w;
            px = g_gui.layout_base_x;
            py = g_gui.layout_base_y + g_gui.layout_index * (pref_h + g_gui.layout_gap);
            pw = cell_w;
            g_gui.layout_index++;
            break;
        }
        case GUI_LAYOUT_GRID_MODE: {
            int cols = g_gui.layout_cols > 0 ? g_gui.layout_cols : 2;
            int col = g_gui.layout_index % cols;
            int row = g_gui.layout_index / cols;
            int cell_w = g_gui.layout_grid_cell_w > 0 ? g_gui.layout_grid_cell_w : pref_w;
            int cell_h = g_gui.layout_grid_cell_h > 0 ? g_gui.layout_grid_cell_h : pref_h;
            px = g_gui.layout_base_x + col * (cell_w + g_gui.layout_gap);
            py = g_gui.layout_base_y + row * (cell_h + g_gui.layout_gap);
            pw = cell_w;
            ph = cell_h;
            g_gui.layout_index++;
            break;
        }
        case GUI_LAYOUT_FLOW:
        default:
            g_gui.layout_y += pref_h + g_gui.layout_gap;
            g_gui.layout_index++;
            break;
    }

    g_gui.layout_x = px;
    g_gui.layout_y = py + ph + g_gui.layout_gap;

    if (x) *x = px;
    if (y) *y = py;
    if (w) *w = pw;
    if (h) *h = ph;
}
static HVM_InputState* gui_get_input_state(HVM_GuiRuntime *gui, const char *label) {
    size_t i;
    if (!label) label = "";
    for (i = 0; i < g_gui.input_count; i++) {
        if (g_gui.inputs[i].label && strcmp(g_gui.inputs[i].label, label) == 0) {
            return &g_gui.inputs[i];
        }
    }

    if (g_gui.input_count == g_gui.input_cap) {
        size_t new_cap = g_gui.input_cap == 0 ? 8 : g_gui.input_cap * 2;
        HVM_InputState *ni = (HVM_InputState *)realloc(g_gui.inputs, new_cap * sizeof(HVM_InputState));
        if (!ni) return NULL;
        memset(ni + g_gui.input_cap, 0, (new_cap - g_gui.input_cap) * sizeof(HVM_InputState));
        g_gui.inputs = ni;
        g_gui.input_cap = new_cap;
    }

    g_gui.inputs[g_gui.input_count].label = strdup(label);
    if (!g_gui.inputs[g_gui.input_count].label) return NULL;
    g_gui.inputs[g_gui.input_count].buffer = (char *)calloc(1, 1);
    if (!g_gui.inputs[g_gui.input_count].buffer) {
        free(g_gui.inputs[g_gui.input_count].label);
        g_gui.inputs[g_gui.input_count].label = NULL;
        return NULL;
    }
    g_gui.inputs[g_gui.input_count].len = 0;
    g_gui.inputs[g_gui.input_count].cap = 1;
    g_gui.input_count++;
    return &g_gui.inputs[g_gui.input_count - 1];
}

static HVM_TextAreaState* gui_get_textarea_state(HVM_GuiRuntime *gui, const char *id) {
    size_t i;
    if (!id) id = "";

    for (i = 0; i < g_gui.textarea_count; i++) {
        if (g_gui.textareas[i].id && strcmp(g_gui.textareas[i].id, id) == 0) {
            return &g_gui.textareas[i];
        }
    }

    if (g_gui.textarea_count == g_gui.textarea_cap) {
        size_t new_cap = g_gui.textarea_cap == 0 ? 4 : g_gui.textarea_cap * 2;
        HVM_TextAreaState *nt = (HVM_TextAreaState *)realloc(g_gui.textareas, new_cap * sizeof(HVM_TextAreaState));
        if (!nt) return NULL;
        memset(nt + g_gui.textarea_cap, 0, (new_cap - g_gui.textarea_cap) * sizeof(HVM_TextAreaState));
        g_gui.textareas = nt;
        g_gui.textarea_cap = new_cap;
    }

    g_gui.textareas[g_gui.textarea_count].id = strdup(id);
    if (!g_gui.textareas[g_gui.textarea_count].id) return NULL;
    g_gui.textareas[g_gui.textarea_count].buffer = (char *)calloc(1, 1);
    if (!g_gui.textareas[g_gui.textarea_count].buffer) {
        free(g_gui.textareas[g_gui.textarea_count].id);
        g_gui.textareas[g_gui.textarea_count].id = NULL;
        return NULL;
    }
    g_gui.textareas[g_gui.textarea_count].len = 0;
    g_gui.textareas[g_gui.textarea_count].cap = 1;
    g_gui.textarea_count++;
    return &g_gui.textareas[g_gui.textarea_count - 1];
}

static int gui_push_cmd(HVM_GuiRuntime *gui, const HVM_GuiCmd *cmd) {
    size_t new_cap;
    HVM_GuiCmd *nc;
    if (!cmd) return 0;
    if (g_gui.cmd_count == g_gui.cmd_cap) {
        new_cap = g_gui.cmd_cap == 0 ? 32 : g_gui.cmd_cap * 2;
        nc = (HVM_GuiCmd *)realloc(g_gui.cmds, new_cap * sizeof(HVM_GuiCmd));
        if (!nc) return 0;
        memset(nc + g_gui.cmd_cap, 0, (new_cap - g_gui.cmd_cap) * sizeof(HVM_GuiCmd));
        g_gui.cmds = nc;
        g_gui.cmd_cap = new_cap;
    }
    g_gui.cmds[g_gui.cmd_count] = *cmd;
    if (cmd->text) {
        g_gui.cmds[g_gui.cmd_count].text = strdup(cmd->text);
        if (!g_gui.cmds[g_gui.cmd_count].text) return 0;
    }
    g_gui.cmd_count++;
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

static void gui_reset_style_defaults(HVM_GuiRuntime *gui) {
    g_gui.fg_color = 0x000000;
    g_gui.bg_color = 0x00FFFFFF;
    g_gui.font_size = 18;

    g_gui.layout_x = 16;
    g_gui.layout_y = 16;
    g_gui.layout_base_x = 16;
    g_gui.layout_base_y = 16;
    g_gui.layout_gap = 8;
    g_gui.layout_mode = GUI_LAYOUT_FLOW;
    g_gui.layout_index = 0;
    g_gui.layout_cols = 2;
    g_gui.layout_col_width = 180;
    g_gui.layout_row_height = 32;
    g_gui.layout_grid_cell_w = 180;
    g_gui.layout_grid_cell_h = 32;

    g_gui.scroll_y = 0;
    g_gui.scroll_range = 0;
    g_gui.menu_ready = 0;
    g_gui.last_menu_cmd = 0;

    g_gui.active_input = GUI_NO_ACTIVE_INPUT;
    g_gui.active_textarea = GUI_NO_ACTIVE_TEXTAREA;

    g_gui.mouse_down = 0;
    g_gui.mouse_up = 0;
    g_gui.mouse_clicked = 0;
    g_gui.mouse_click_consumed = 0;
    g_gui.mouse_click_x = 0;
    g_gui.mouse_click_y = 0;
    g_gui.mouse_x = 0;
    g_gui.mouse_y = 0;

    memset(g_gui.key_down, 0, sizeof(g_gui.key_down));
    memset(g_gui.key_pressed, 0, sizeof(g_gui.key_pressed));

    if (g_gui.frame_index == 0) g_gui.frame_index = 1;

#ifdef _WIN32
    if (g_gui.font) {
        DeleteObject(g_gui.font);
        g_gui.font = NULL;
    }
    g_gui.font = CreateFontA(g_gui.font_size, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, ANSI_CHARSET,
                             OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, DEFAULT_QUALITY,
                             DEFAULT_PITCH | FF_DONTCARE, "Segoe UI");
#endif
}

#ifdef _WIN32
static const char *HVM_GUI_CLASS_NAME = "HOSCVMWindowClass";

static int gui_headless_mode(void) {
    const char *env = getenv("HVM_GUI_HEADLESS");
    if (!env || env[0] == '\0') return 0;
    if (strcmp(env, "1") == 0 || strcmp(env, "true") == 0 || strcmp(env, "yes") == 0) return 1;
    return 0;
}

static void gui_update_scrollbar(HVM_GuiRuntime *gui) {
    RECT client;
    SCROLLINFO si;
    int page;
    int max_pos;

    if (!g_gui.hwnd) return;

    GetClientRect(g_gui.hwnd, &client);
    page = client.bottom - client.top;
    if (page < 1) page = 1;

    if (g_gui.scroll_range < 0) g_gui.scroll_range = 0;
    max_pos = g_gui.scroll_range - page;
    if (max_pos < 0) max_pos = 0;

    if (g_gui.scroll_y < 0) g_gui.scroll_y = 0;
    if (g_gui.scroll_y > max_pos) g_gui.scroll_y = max_pos;

    memset(&si, 0, sizeof(si));
    si.cbSize = sizeof(si);
    si.fMask = SIF_RANGE | SIF_PAGE | SIF_POS;
    si.nMin = 0;
    si.nMax = g_gui.scroll_range;
    si.nPage = (UINT)page;
    si.nPos = g_gui.scroll_y;
    SetScrollInfo(g_gui.hwnd, SB_VERT, &si, TRUE);
}

static void gui_scroll_to(HVM_GuiRuntime *gui, int new_pos) {
    g_gui.scroll_y = new_pos;
    gui_update_scrollbar(gui);
}

static void gui_handle_vscroll(HVM_GuiRuntime *gui, WPARAM wparam) {
    SCROLLINFO si;
    int pos;
    int max_pos;

    if (!g_gui.hwnd) return;

    memset(&si, 0, sizeof(si));
    si.cbSize = sizeof(si);
    si.fMask = SIF_ALL;
    GetScrollInfo(g_gui.hwnd, SB_VERT, &si);

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

    if (pos != g_gui.scroll_y) {
        gui_scroll_to(gui, pos);
        InvalidateRect(g_gui.hwnd, NULL, FALSE);
    }
}

static int gui_menu_setup_notepad(HVM_GuiRuntime *gui) {
    HMENU old_menu;
    HMENU menu_bar;
    HMENU file_menu;

    if (!g_gui.hwnd) return 0;

    old_menu = GetMenu(g_gui.hwnd);
    if (old_menu) {
        SetMenu(g_gui.hwnd, NULL);
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

    SetMenu(g_gui.hwnd, menu_bar);
    DrawMenuBar(g_gui.hwnd);
    g_gui.menu_ready = 1;
    return 1;
}

static int gui_take_menu_event(HVM_GuiRuntime *gui) {
    int cmd = g_gui.last_menu_cmd;
    g_gui.last_menu_cmd = 0;
    return cmd;
}

static char *gui_file_dialog(HVM_GuiRuntime *gui, int save_mode) {
    OPENFILENAMEA ofn;
    char path[MAX_PATH];
    BOOL ok;

    if (!g_gui.hwnd) return NULL;

    memset(path, 0, sizeof(path));
    memset(&ofn, 0, sizeof(ofn));
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = g_gui.hwnd;
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

static char *gui_open_file_dialog(HVM_GuiRuntime *gui) {
    return gui_file_dialog(gui, 0);
}

static char *gui_save_file_dialog(HVM_GuiRuntime *gui) {
    return gui_file_dialog(gui, 1);
}

static LRESULT CALLBACK hvm_gui_wndproc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam) {
    HVM_GuiRuntime *gui = (HVM_GuiRuntime *)GetWindowLongPtr(hwnd, GWLP_USERDATA);
    if (!gui) {
        return DefWindowProcA(hwnd, msg, wparam, lparam);
    }
    switch (msg) {
        case WM_CLOSE:
            DestroyWindow(hwnd);
            return 0;
        case WM_DESTROY:
            g_gui.running = 0;
            g_gui.hwnd = NULL;
            g_gui.menu_ready = 0;
            g_gui.last_menu_cmd = 0;
            PostQuitMessage(0);
            return 0;
        case WM_SIZE:
            gui_update_scrollbar(gui);
            return 0;
        case WM_COMMAND:
            if (HIWORD(wparam) == 0) {
                int cmd = (int)LOWORD(wparam);
                if (cmd == HVM_MENU_FILE_NEW || cmd == HVM_MENU_FILE_OPEN || cmd == HVM_MENU_FILE_SAVE || cmd == HVM_MENU_FILE_EXIT) {
                    g_gui.last_menu_cmd = cmd;
                    if (cmd == HVM_MENU_FILE_EXIT) {
                        DestroyWindow(hwnd);
                    }
                    return 0;
                }
            }
            break;
        case WM_VSCROLL:
            gui_handle_vscroll(gui, wparam);
            return 0;
        case WM_MOUSEWHEEL: {
            int delta = GET_WHEEL_DELTA_WPARAM(wparam);
            int step = (delta / WHEEL_DELTA) * 40;
            if (step != 0) {
                gui_scroll_to(gui, g_gui.scroll_y - step);
                InvalidateRect(g_gui.hwnd, NULL, FALSE);
            }
            return 0;
        }
        case WM_MOUSEMOVE:
            g_gui.mouse_x = (int)GET_X_LPARAM(lparam);
            g_gui.mouse_y = (int)GET_Y_LPARAM(lparam) + g_gui.scroll_y;
            return 0;
        case WM_LBUTTONDOWN:
            g_gui.mouse_down = 1;
            SetFocus(hwnd);
            return 0;
        case WM_LBUTTONUP:
            g_gui.mouse_down = 0;
            g_gui.mouse_up = 1;
            g_gui.mouse_clicked = 1;
            g_gui.mouse_click_consumed = 0;
            g_gui.mouse_click_x = (int)GET_X_LPARAM(lparam);
            g_gui.mouse_click_y = (int)GET_Y_LPARAM(lparam) + g_gui.scroll_y;
            return 0;
        case WM_KEYDOWN:
            if ((wparam & 0xFF) < 256) {
                int code = (int)(wparam & 0xFF);
                if (!g_gui.key_down[code]) g_gui.key_pressed[code] = 1;
                g_gui.key_down[code] = 1;
            }
            return 0;
        case WM_KEYUP:
            if ((wparam & 0xFF) < 256) g_gui.key_down[wparam & 0xFF] = 0;
            return 0;
        case WM_KILLFOCUS:
            memset(g_gui.key_down, 0, sizeof(g_gui.key_down));
            return 0;
        case WM_CHAR:
            if (g_gui.active_textarea != GUI_NO_ACTIVE_TEXTAREA && g_gui.active_textarea < g_gui.textarea_count) {
                HVM_TextAreaState *ta = &g_gui.textareas[g_gui.active_textarea];
                if (wparam == 8) {
                    if (ta->len > 0) ta->buffer[--ta->len] = '\0';
                } else if (wparam == 13) {
                    if (!gui_buffer_append_char(gui, &ta->buffer, &ta->len, &ta->cap, '\n')) return 0;
                } else if (wparam == 9) {
                    if (!gui_buffer_append_char(gui, &ta->buffer, &ta->len, &ta->cap, ' ')) return 0;
                    if (!gui_buffer_append_char(gui, &ta->buffer, &ta->len, &ta->cap, ' ')) return 0;
                    if (!gui_buffer_append_char(gui, &ta->buffer, &ta->len, &ta->cap, ' ')) return 0;
                    if (!gui_buffer_append_char(gui, &ta->buffer, &ta->len, &ta->cap, ' ')) return 0;
                } else if (wparam >= 32 && wparam < 127) {
                    if (!gui_buffer_append_char(gui, &ta->buffer, &ta->len, &ta->cap, (char)wparam)) return 0;
                }
                if (g_gui.hwnd) InvalidateRect(g_gui.hwnd, NULL, FALSE);
                return 0;
            }

            if (g_gui.active_input != GUI_NO_ACTIVE_INPUT && g_gui.active_input < g_gui.input_count) {
                HVM_InputState *st = &g_gui.inputs[g_gui.active_input];
                if (wparam == 8) {
                    if (st->len > 0) st->buffer[--st->len] = '\0';
                } else if (wparam >= 32 && wparam < 127) {
                    if (!gui_buffer_append_char(gui, &st->buffer, &st->len, &st->cap, (char)wparam)) return 0;
                }
                if (g_gui.hwnd) InvalidateRect(g_gui.hwnd, NULL, FALSE);
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
            int y_offset = g_gui.scroll_y;

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
                HBRUSH bg = CreateSolidBrush(g_gui.bg_color);
                FillRect(mem_dc, &client, bg);
                DeleteObject(bg);
            }

            if (g_gui.font) old_font = SelectObject(mem_dc, g_gui.font);
            SetBkMode(mem_dc, TRANSPARENT);

            for (i = 0; i < g_gui.cmd_count; i++) {
                HVM_GuiCmd *cmd = &g_gui.cmds[i];
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
                        int hovered = gui_point_in_rect(gui, g_gui.mouse_x, g_gui.mouse_y, cmd->x, cmd->y, cmd->w, cmd->h);
                        int down = hovered && g_gui.mouse_down;
                        if (cmd->payload) {
                            size_t idx = GUI_PAYLOAD_TO_INDEX(cmd->payload);
                            if (idx < g_gui.widget_count) ws = &g_gui.widgets[idx];
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
                            if (idx < g_gui.input_count) st = &g_gui.inputs[idx];
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
                                if (idx < g_gui.textarea_count) ta = &g_gui.textareas[idx];
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

static int gui_register_class(HVM_GuiRuntime *gui) {
    WNDCLASSA wc;
    if (g_gui.class_registered) return 1;

    memset(&wc, 0, sizeof(wc));
    wc.lpfnWndProc = hvm_gui_wndproc;
    wc.hInstance = GetModuleHandleA(NULL);
    wc.lpszClassName = HVM_GUI_CLASS_NAME;
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);
    wc.hbrBackground = NULL;

    if (!RegisterClassA(&wc)) {
        return 0;
    }

    g_gui.class_registered = 1;
    return 1;
}

static void gui_pump_events(HVM_GuiRuntime *gui) {
    MSG msg;
    while (PeekMessageA(&msg, NULL, 0, 0, PM_REMOVE)) {
        if (msg.message == WM_QUIT) {
            g_gui.running = 0;
            g_gui.hwnd = NULL;
            continue;
        }
        TranslateMessage(&msg);
        DispatchMessageA(&msg);
    }
}

static void gui_request_repaint(HVM_GuiRuntime *gui) {
    if (!g_gui.hwnd) return;
    InvalidateRect(g_gui.hwnd, NULL, FALSE);
    /* paint is handled by message loop */
}

static int gui_create_window(HVM_GuiRuntime *gui, const char *title) {
    if (gui_headless_mode()) return 0;
    if (!gui_register_class(gui)) return 0;

    while (1) {
        MSG msg;
        if (!PeekMessageA(&msg, NULL, WM_QUIT, WM_QUIT, PM_REMOVE)) break;
    }

    if (g_gui.hwnd) {
        DestroyWindow(g_gui.hwnd);
        g_gui.hwnd = NULL;
    }

    gui_free_commands(gui);
    gui_free_inputs(gui);
    gui_free_textareas(gui);
    gui_free_widgets(gui);
    gui_reset_style_defaults(gui);
    g_gui.last_tick = (uint64_t)gui_now_ms();
    g_gui.frame_index = 1;

    g_gui.hwnd = CreateWindowExA(
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

    if (!g_gui.hwnd) {
        return 0;
    }

    SetWindowLongPtrA(g_gui.hwnd, GWLP_USERDATA, (LONG_PTR)gui);
    ShowWindow(g_gui.hwnd, SW_SHOW);
    gui_update_scrollbar(gui);
    /* paint is handled by message loop */
    g_gui.using_real = 1;
    g_gui.running = 1;
    return 1;
}

static void gui_run_loop_until_close(HVM_GuiRuntime *gui) {
    while (g_gui.running && g_gui.hwnd) {
        gui_reset_transient_input(gui);
        gui_pump_events(gui);
        if (!g_gui.running || !g_gui.hwnd) break;

        gui_apply_click_focus_from_commands(gui);
        gui_request_repaint(gui);
        Sleep(16);
    }
}
#else
static int gui_headless_mode(void) { return 1; }
static void gui_pump_events(HVM_GuiRuntime *gui) { (void)gui; }
static void gui_request_repaint(HVM_GuiRuntime *gui) { (void)gui; }
static int gui_create_window(HVM_GuiRuntime *gui, const char *title) { (void)gui; (void)title; return 0; }
static void gui_run_loop_until_close(HVM_GuiRuntime *gui) { (void)gui; }
static int gui_menu_setup_notepad(HVM_GuiRuntime *gui) { (void)gui; return 0; }
static int gui_take_menu_event(HVM_GuiRuntime *gui) { (void)gui; return 0; }
static char *gui_open_file_dialog(HVM_GuiRuntime *gui) { (void)gui; return NULL; }
static char *gui_save_file_dialog(HVM_GuiRuntime *gui) { (void)gui; return NULL; }
#endif

static void gui_shutdown(HVM_GuiRuntime *gui) {
#ifdef _WIN32
    if (g_gui.hwnd) {
        DestroyWindow(g_gui.hwnd);
        g_gui.hwnd = NULL;
    }
#endif
    gui_free_commands(gui);
    gui_free_inputs(gui);
    gui_free_textareas(gui);
    gui_free_widgets(gui);
    gui_reset_style_defaults(gui);
    g_gui.last_tick = (uint64_t)gui_now_ms();
    g_gui.frame_index = 1;
    g_gui.using_real = 0;
    g_gui.running = 0;
    g_gui.loop_called = 0;
}








/* GUI opcode dispatch */
typedef int (*HVM_GuiOpcodeHandler)(HVM_GuiRuntime *gui, HVM_VM *vm, const HVM_Instruction *instr);

static int gui_op_create_window(HVM_GuiRuntime *gui, HVM_VM *vm, const HVM_Instruction *instr) {
    int success = gui_create_window(gui, instr->operand.string_operand ? instr->operand.string_operand : "HOSC VM Window");
    if (success) {
        GUI_LOG("[GUI] real window: %s\n", instr->operand.string_operand ? instr->operand.string_operand : "HOSC VM Window");
    } else {
        g_gui.using_real = 0;
        g_gui.running = 1;
        gui_free_commands(gui);
        gui_free_inputs(gui);
        gui_free_textareas(gui);
        gui_free_widgets(gui);
        gui_reset_style_defaults(gui);
        g_gui.last_tick = (uint64_t)gui_now_ms();
        g_gui.frame_index = 1;
        GUI_LOG("[GUI] window (console fallback): %s\n", instr->operand.string_operand ? instr->operand.string_operand : "HOSC VM Window");
    }
    hvm_push_bool(vm, success);
    return 1;
}

static int gui_op_clear(HVM_GuiRuntime *gui, HVM_VM *vm, const HVM_Instruction *instr) {
    HVM_Value b, g, r;
    int ri, gi, bi;
    (void)instr;
    if (vm->stack_top < 3) { hvm_set_error_msg(vm, "Stack underflow in CLEAR"); vm->running = 0; return 1; }
    b = hvm_pop(vm); g = hvm_pop(vm); r = hvm_pop(vm);
    ri = (r.type == HVM_TYPE_FLOAT) ? (int)r.data.float_value : (int)r.data.int_value;
    gi = (g.type == HVM_TYPE_FLOAT) ? (int)g.data.float_value : (int)g.data.int_value;
    bi = (b.type == HVM_TYPE_FLOAT) ? (int)b.data.float_value : (int)b.data.int_value;
    g_gui.bg_color = RGB((ri)&0xFF, (gi)&0xFF, (bi)&0xFF);
    gui_clear_commands(gui);
    if (g_gui.using_real && g_gui.running) { /* repaint in HVM_LOOP */ }
    hvm_push_int(vm, 0);
    hvm_free_value(&b); hvm_free_value(&g); hvm_free_value(&r);
    return 1;
}

static int gui_op_set_bg_color(HVM_GuiRuntime *gui, HVM_VM *vm, const HVM_Instruction *instr) {
    HVM_Value b, g, r;
    int ri, gi, bi;
    (void)instr;
    if (vm->stack_top < 3) { hvm_set_error_msg(vm, "Stack underflow in SET_BG_COLOR"); vm->running = 0; return 1; }
    b = hvm_pop(vm); g = hvm_pop(vm); r = hvm_pop(vm);
    ri = (r.type == HVM_TYPE_FLOAT) ? (int)r.data.float_value : (int)r.data.int_value;
    gi = (g.type == HVM_TYPE_FLOAT) ? (int)g.data.float_value : (int)g.data.int_value;
    bi = (b.type == HVM_TYPE_FLOAT) ? (int)b.data.float_value : (int)b.data.int_value;
    g_gui.bg_color = RGB((ri)&0xFF, (gi)&0xFF, (bi)&0xFF);
    if (g_gui.using_real && g_gui.running) { /* repaint in HVM_LOOP */ }
    hvm_push_int(vm, 0);
    hvm_free_value(&b); hvm_free_value(&g); hvm_free_value(&r);
    return 1;
}

static int gui_op_set_color(HVM_GuiRuntime *gui, HVM_VM *vm, const HVM_Instruction *instr) {
    HVM_Value b, g, r;
    int ri, gi, bi;
    (void)instr;
    if (vm->stack_top < 3) { hvm_set_error_msg(vm, "Stack underflow in SET_COLOR"); vm->running = 0; return 1; }
    b = hvm_pop(vm); g = hvm_pop(vm); r = hvm_pop(vm);
    ri = (r.type == HVM_TYPE_FLOAT) ? (int)r.data.float_value : (int)r.data.int_value;
    gi = (g.type == HVM_TYPE_FLOAT) ? (int)g.data.float_value : (int)g.data.int_value;
    bi = (b.type == HVM_TYPE_FLOAT) ? (int)b.data.float_value : (int)b.data.int_value;
    g_gui.fg_color = RGB((ri)&0xFF, (gi)&0xFF, (bi)&0xFF);
    hvm_push_int(vm, 0);
    hvm_free_value(&b); hvm_free_value(&g); hvm_free_value(&r);
    return 1;
}

static int gui_op_set_font_size(HVM_GuiRuntime *gui, HVM_VM *vm, const HVM_Instruction *instr) {
    HVM_Value v;
    int size;
    (void)instr;
    if (vm->stack_top < 1) { hvm_set_error_msg(vm, "Stack underflow in SET_FONT_SIZE"); vm->running = 0; return 1; }
    v = hvm_pop(vm);
    size = (v.type == HVM_TYPE_FLOAT) ? (int)v.data.float_value : (int)v.data.int_value;
    if (size < 8) size = 8;
    if (size > 96) size = 96;
    g_gui.font_size = size;
#ifdef _WIN32
    if (g_gui.font) { DeleteObject(g_gui.font); g_gui.font = NULL; }
    g_gui.font = CreateFontA(g_gui.font_size, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, ANSI_CHARSET,
                             OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, DEFAULT_QUALITY,
                             DEFAULT_PITCH | FF_DONTCARE, "Segoe UI");
#endif
    hvm_push_int(vm, 0);
    hvm_free_value(&v);
    return 1;
}

static int gui_op_draw_text(HVM_GuiRuntime *gui, HVM_VM *vm, const HVM_Instruction *instr) {
    HVM_Value msg, y, x;
    const char *text;
    long long xi, yi;
    HVM_GuiCmd cmd;
    (void)instr;
    if (vm->stack_top < 3) { hvm_set_error_msg(vm, "Stack underflow in DRAW_TEXT"); vm->running = 0; return 1; }
    msg = hvm_pop(vm); y = hvm_pop(vm); x = hvm_pop(vm);
    text = (msg.type == HVM_TYPE_STRING && msg.data.string_value) ? msg.data.string_value : "";
    xi = (x.type == HVM_TYPE_FLOAT) ? (long long)x.data.float_value : (long long)x.data.int_value;
    yi = (y.type == HVM_TYPE_FLOAT) ? (long long)y.data.float_value : (long long)y.data.int_value;
    memset(&cmd, 0, sizeof(cmd));
    cmd.type = GUI_CMD_TEXT;
    cmd.x = (int)xi; cmd.y = (int)yi; cmd.color = g_gui.fg_color; cmd.text = (char *)text;
    if (g_gui.using_real && g_gui.running) {
        if (!gui_push_cmd(gui, &cmd)) { hvm_set_error_msg(vm, "Out of memory while queueing GUI text"); vm->running = 0; }
        else { /* repaint in HVM_LOOP */ }
    } else if (!g_gui.using_real) {
        GUI_LOG("[GUI] text (%lld,%lld): %s\n", xi, yi, text);
    }
    hvm_push_int(vm, 0);
    hvm_free_value(&msg); hvm_free_value(&y); hvm_free_value(&x);
    return 1;
}

static int gui_op_draw_button(HVM_GuiRuntime *gui, HVM_VM *vm, const HVM_Instruction *instr) {
    HVM_Value label, h, w, y, x;
    long long xi, yi, wi, hi;
    int clicked = 0;
    HVM_GuiCmd cmd;
    HVM_WidgetState *ws;
    size_t widx = (size_t)(-1);
    const char *button_id;
    (void)instr;
    if (vm->stack_top < 5) { hvm_set_error_msg(vm, "Stack underflow in DRAW_BUTTON"); vm->running = 0; return 1; }
    label = hvm_pop(vm); h = hvm_pop(vm); w = hvm_pop(vm); y = hvm_pop(vm); x = hvm_pop(vm);
    xi = (x.type == HVM_TYPE_FLOAT) ? (long long)x.data.float_value : (long long)x.data.int_value;
    yi = (y.type == HVM_TYPE_FLOAT) ? (long long)y.data.float_value : (long long)y.data.int_value;
    wi = (w.type == HVM_TYPE_FLOAT) ? (long long)w.data.float_value : (long long)w.data.int_value;
    hi = (h.type == HVM_TYPE_FLOAT) ? (long long)h.data.float_value : (long long)h.data.int_value;
    button_id = (label.type == HVM_TYPE_STRING && label.data.string_value) ? label.data.string_value : "";
    ws = gui_register_widget_rect(gui, button_id, (int)xi, (int)yi, (int)wi, (int)hi);
    if (ws) widx = (size_t)(ws - g_gui.widgets);
    clicked = gui_take_mouse_click_in_rect(gui, (int)xi, (int)yi, (int)wi, (int)hi);
    if (ws) ws->clicked = clicked;
    memset(&cmd, 0, sizeof(cmd));
    cmd.type = GUI_CMD_BUTTON;
    cmd.x = (int)xi;
    cmd.y = (int)yi;
    cmd.w = (int)wi;
    cmd.h = (int)hi;
    cmd.text = (char *)button_id;
    cmd.payload = (ws && widx != (size_t)(-1)) ? GUI_PAYLOAD_INDEX(widx) : NULL;
    if (g_gui.using_real && g_gui.running) {
        if (!gui_push_cmd(gui, &cmd)) { hvm_set_error_msg(vm, "Out of memory while queueing button"); vm->running = 0; }
    } else if (!g_gui.using_real) {
        GUI_LOG("[GUI] button (%lld,%lld,%lld,%lld): %s\n", xi, yi, wi, hi, cmd.text ? cmd.text : "");
    }
    hvm_push_bool(vm, clicked);
    hvm_free_value(&label); hvm_free_value(&h); hvm_free_value(&w); hvm_free_value(&y); hvm_free_value(&x);
    return 1;
}

static int gui_op_draw_button_state(HVM_GuiRuntime *gui, HVM_VM *vm, const HVM_Instruction *instr) {
    HVM_Value label_value, id_value;
    const char *id_name = NULL;
    const char *label_text = NULL;
    int bx, by, bw, bh;
    int clicked;
    HVM_GuiCmd cmd;
    HVM_WidgetState *ws;
    size_t widx = (size_t)(-1);
    (void)instr;

    if (vm->stack_top < 2) { hvm_set_error_msg(vm, "Stack underflow in DRAW_BUTTON_STATE"); vm->running = 0; return 1; }
    label_value = hvm_pop(vm);
    id_value = hvm_pop(vm);

    if (id_value.type == HVM_TYPE_STRING) id_name = id_value.data.string_value ? id_value.data.string_value : "";
    else id_name = hvm_value_to_cstring_a(vm, id_value);

    if (label_value.type == HVM_TYPE_STRING) label_text = label_value.data.string_value ? label_value.data.string_value : "Button";
    else label_text = hvm_value_to_cstring_b(vm, label_value);

    if (!id_name || !label_text) {
        hvm_free_value(&label_value);
        hvm_free_value(&id_value);
        hvm_set_error_msg(vm, "Out of memory in DRAW_BUTTON_STATE");
        vm->running = 0;
        return 1;
    }

    gui_layout_place_widget(gui, 180, 34, &bx, &by, &bw, &bh);
    ws = gui_register_widget_rect(gui, id_name, bx, by, bw, bh);
    if (ws) widx = (size_t)(ws - g_gui.widgets);

    clicked = gui_take_mouse_click_in_rect(gui, bx, by, bw, bh);
    if (ws) ws->clicked = clicked;

    memset(&cmd, 0, sizeof(cmd));
    cmd.type = GUI_CMD_BUTTON;
    cmd.x = bx;
    cmd.y = by;
    cmd.w = bw;
    cmd.h = bh;
    cmd.text = (char *)label_text;
    cmd.payload = (ws && widx != (size_t)(-1)) ? GUI_PAYLOAD_INDEX(widx) : NULL;

    if (g_gui.using_real && g_gui.running) {
        if (!gui_push_cmd(gui, &cmd)) { hvm_set_error_msg(vm, "Out of memory while queueing state button"); vm->running = 0; }
    } else if (!g_gui.using_real) {
        GUI_LOG("[GUI] button(id=%s) (%d,%d,%d,%d): %s\n", id_name, bx, by, bw, bh, label_text);
    }

    hvm_push_bool(vm, clicked);

    hvm_free_value(&label_value);
    hvm_free_value(&id_value);
    return 1;
}

static int gui_op_draw_input(HVM_GuiRuntime *gui, HVM_VM *vm, const HVM_Instruction *instr) {
    HVM_Value label, w, y, x;
    long long xi, yi, wi;
    HVM_GuiCmd cmd;
    HVM_InputState *st;
    size_t idx;
    (void)instr;
    if (vm->stack_top < 4) { hvm_set_error_msg(vm, "Stack underflow in DRAW_INPUT"); vm->running = 0; return 1; }
    label = hvm_pop(vm); w = hvm_pop(vm); y = hvm_pop(vm); x = hvm_pop(vm);
    xi = (x.type == HVM_TYPE_FLOAT) ? (long long)x.data.float_value : (long long)x.data.int_value;
    yi = (y.type == HVM_TYPE_FLOAT) ? (long long)y.data.float_value : (long long)y.data.int_value;
    wi = (w.type == HVM_TYPE_FLOAT) ? (long long)w.data.float_value : (long long)w.data.int_value;
    st = gui_get_input_state(gui, (label.type == HVM_TYPE_STRING && label.data.string_value) ? label.data.string_value : "");
    idx = st ? (size_t)(st - g_gui.inputs) : GUI_NO_ACTIVE_INPUT;
    memset(&cmd, 0, sizeof(cmd));
    cmd.type = GUI_CMD_INPUT;
    cmd.x = (int)xi;
    cmd.y = (int)yi;
    cmd.w = (int)wi;
    cmd.h = 28;
    cmd.text = (label.type == HVM_TYPE_STRING && label.data.string_value) ? label.data.string_value : "";
    cmd.payload = (idx != GUI_NO_ACTIVE_INPUT) ? GUI_PAYLOAD_INDEX(idx) : NULL;
    if (gui_take_mouse_focus_in_rect(gui, cmd.x, cmd.y, cmd.w, cmd.h)) {
        g_gui.active_input = idx;
        g_gui.active_textarea = GUI_NO_ACTIVE_TEXTAREA;
    }
    if (g_gui.using_real && g_gui.running) {
        if (!gui_push_cmd(gui, &cmd)) { hvm_set_error_msg(vm, "Out of memory while queueing input"); vm->running = 0; }
    } else if (!g_gui.using_real) {
        GUI_LOG("[GUI] input (%lld,%lld,%lld): %s\n", xi, yi, wi, cmd.text ? cmd.text : "");
    }
    if (st && st->buffer) hvm_push_string(vm, st->buffer); else hvm_push_string(vm, "");
    hvm_free_value(&label); hvm_free_value(&w); hvm_free_value(&y); hvm_free_value(&x);
    return 1;
}

static int gui_op_draw_input_state(HVM_GuiRuntime *gui, HVM_VM *vm, const HVM_Instruction *instr) {
    HVM_Value placeholder_value, id_value;
    const char *id_name = NULL;
    const char *placeholder = NULL;
    HVM_GuiCmd cmd;
    HVM_InputState *st;
    size_t idx;
    int ix, iy, iw, ih;
    (void)instr;

    if (vm->stack_top < 2) { hvm_set_error_msg(vm, "Stack underflow in DRAW_INPUT_STATE"); vm->running = 0; return 1; }
    placeholder_value = hvm_pop(vm);
    id_value = hvm_pop(vm);

    if (id_value.type == HVM_TYPE_STRING) id_name = id_value.data.string_value ? id_value.data.string_value : "";
    else id_name = hvm_value_to_cstring_a(vm, id_value);

    if (placeholder_value.type == HVM_TYPE_STRING) placeholder = placeholder_value.data.string_value ? placeholder_value.data.string_value : "";
    else placeholder = hvm_value_to_cstring_b(vm, placeholder_value);

    if (!id_name || !placeholder) {
        hvm_free_value(&placeholder_value);
        hvm_free_value(&id_value);
        hvm_set_error_msg(vm, "Out of memory in DRAW_INPUT_STATE");
        vm->running = 0;
        return 1;
    }

    st = gui_get_input_state(gui, id_name);
    idx = st ? (size_t)(st - g_gui.inputs) : GUI_NO_ACTIVE_INPUT;
    if (st && st->len == 0 && placeholder[0] != '\0') {
        gui_buffer_set(gui, &st->buffer, &st->len, &st->cap, placeholder);
    }

    gui_layout_place_widget(gui, 260, 30, &ix, &iy, &iw, &ih);
    gui_register_widget_rect(gui, id_name, ix, iy, iw, ih);

    memset(&cmd, 0, sizeof(cmd));
    cmd.type = GUI_CMD_INPUT;
    cmd.x = ix;
    cmd.y = iy;
    cmd.w = iw;
    cmd.h = ih;
    cmd.text = (char *)placeholder;
    cmd.payload = (idx != GUI_NO_ACTIVE_INPUT) ? GUI_PAYLOAD_INDEX(idx) : NULL;

    if (gui_take_mouse_focus_in_rect(gui, cmd.x, cmd.y, cmd.w, cmd.h)) {
        g_gui.active_input = idx;
        g_gui.active_textarea = GUI_NO_ACTIVE_TEXTAREA;
    }

    if (g_gui.using_real && g_gui.running) {
        if (!gui_push_cmd(gui, &cmd)) { hvm_set_error_msg(vm, "Out of memory while queueing state input"); vm->running = 0; }
    } else if (!g_gui.using_real) {
        GUI_LOG("[GUI] input(id=%s) (%d,%d,%d): %s\n", id_name, ix, iy, iw, placeholder);
    }

    if (st && st->buffer) hvm_push_string(vm, st->buffer); else hvm_push_string(vm, "");

    hvm_free_value(&placeholder_value);
    hvm_free_value(&id_value);
    return 1;
}

static int gui_op_draw_textarea(HVM_GuiRuntime *gui, HVM_VM *vm, const HVM_Instruction *instr) {
    HVM_Value id, h, w, y, x;
    long long xi, yi, wi, hi;
    HVM_GuiCmd cmd;
    HVM_TextAreaState *ta;
    size_t idx;
    (void)instr;
    if (vm->stack_top < 5) { hvm_set_error_msg(vm, "Stack underflow in DRAW_TEXTAREA"); vm->running = 0; return 1; }
    id = hvm_pop(vm); h = hvm_pop(vm); w = hvm_pop(vm); y = hvm_pop(vm); x = hvm_pop(vm);
    xi = (x.type == HVM_TYPE_FLOAT) ? (long long)x.data.float_value : (long long)x.data.int_value;
    yi = (y.type == HVM_TYPE_FLOAT) ? (long long)y.data.float_value : (long long)y.data.int_value;
    wi = (w.type == HVM_TYPE_FLOAT) ? (long long)w.data.float_value : (long long)w.data.int_value;
    hi = (h.type == HVM_TYPE_FLOAT) ? (long long)h.data.float_value : (long long)h.data.int_value;
    if (hi < 40) hi = 40;
    ta = gui_get_textarea_state(gui, (id.type == HVM_TYPE_STRING && id.data.string_value) ? id.data.string_value : "");
    idx = ta ? (size_t)(ta - g_gui.textareas) : GUI_NO_ACTIVE_TEXTAREA;
    memset(&cmd, 0, sizeof(cmd));
    cmd.type = GUI_CMD_TEXTAREA;
    cmd.x = (int)xi;
    cmd.y = (int)yi;
    cmd.w = (int)wi;
    cmd.h = (int)hi;
    cmd.text = (id.type == HVM_TYPE_STRING && id.data.string_value) ? id.data.string_value : "";
    cmd.payload = (idx != GUI_NO_ACTIVE_TEXTAREA) ? GUI_PAYLOAD_INDEX(idx) : NULL;
    if (gui_take_mouse_focus_in_rect(gui, cmd.x, cmd.y, cmd.w, cmd.h)) {
        g_gui.active_textarea = idx;
        g_gui.active_input = GUI_NO_ACTIVE_INPUT;
    }
    if (g_gui.using_real && g_gui.running) {
        if (!gui_push_cmd(gui, &cmd)) { hvm_set_error_msg(vm, "Out of memory while queueing textarea"); vm->running = 0; }
    } else if (!g_gui.using_real) {
        GUI_LOG("[GUI] textarea (%lld,%lld,%lld,%lld): %s\n", xi, yi, wi, hi, cmd.text ? cmd.text : "");
    }
    if (ta && ta->buffer) hvm_push_string(vm, ta->buffer); else hvm_push_string(vm, "");
    hvm_free_value(&id); hvm_free_value(&h); hvm_free_value(&w); hvm_free_value(&y); hvm_free_value(&x);
    return 1;
}

static int gui_op_draw_image(HVM_GuiRuntime *gui, HVM_VM *vm, const HVM_Instruction *instr) {
    HVM_Value path, h, w, y, x;
    long long xi, yi, wi, hi;
    HVM_GuiCmd cmd;
    (void)instr;
    if (vm->stack_top < 5) { hvm_set_error_msg(vm, "Stack underflow in DRAW_IMAGE"); vm->running = 0; return 1; }
    path = hvm_pop(vm); h = hvm_pop(vm); w = hvm_pop(vm); y = hvm_pop(vm); x = hvm_pop(vm);
    xi = (x.type == HVM_TYPE_FLOAT) ? (long long)x.data.float_value : (long long)x.data.int_value;
    yi = (y.type == HVM_TYPE_FLOAT) ? (long long)y.data.float_value : (long long)y.data.int_value;
    wi = (w.type == HVM_TYPE_FLOAT) ? (long long)w.data.float_value : (long long)w.data.int_value;
    hi = (h.type == HVM_TYPE_FLOAT) ? (long long)h.data.float_value : (long long)h.data.int_value;
    memset(&cmd, 0, sizeof(cmd));
    cmd.type = GUI_CMD_IMAGE;
    cmd.x = (int)xi; cmd.y = (int)yi; cmd.w = (int)wi; cmd.h = (int)hi;
    cmd.text = (path.type == HVM_TYPE_STRING && path.data.string_value) ? path.data.string_value : "[img]";
    if (g_gui.using_real && g_gui.running) {
        if (!gui_push_cmd(gui, &cmd)) { hvm_set_error_msg(vm, "Out of memory while queueing image"); vm->running = 0; }
        else { /* repaint in HVM_LOOP */ }
    } else if (!g_gui.using_real) {
        GUI_LOG("[GUI] image (%lld,%lld,%lld,%lld): %s\n", xi, yi, wi, hi, cmd.text);
    }
    hvm_push_int(vm, 0);
    hvm_free_value(&path); hvm_free_value(&h); hvm_free_value(&w); hvm_free_value(&y); hvm_free_value(&x);
    return 1;
}

static int gui_op_get_mouse_x(HVM_GuiRuntime *gui, HVM_VM *vm, const HVM_Instruction *instr) {
    (void)gui; (void)instr;
    hvm_push_int(vm, g_gui.mouse_x);
    return 1;
}

static int gui_op_get_mouse_y(HVM_GuiRuntime *gui, HVM_VM *vm, const HVM_Instruction *instr) {
    (void)gui; (void)instr;
    hvm_push_int(vm, g_gui.mouse_y);
    return 1;
}

static int gui_op_is_mouse_down(HVM_GuiRuntime *gui, HVM_VM *vm, const HVM_Instruction *instr) {
    (void)gui; (void)instr;
    hvm_push_bool(vm, g_gui.mouse_down);
    return 1;
}

static int gui_op_was_mouse_up(HVM_GuiRuntime *gui, HVM_VM *vm, const HVM_Instruction *instr) {
    (void)gui; (void)instr;
    hvm_push_bool(vm, g_gui.mouse_up);
    g_gui.mouse_up = 0;
    return 1;
}

static int gui_op_was_mouse_click(HVM_GuiRuntime *gui, HVM_VM *vm, const HVM_Instruction *instr) {
    (void)gui; (void)instr;
    hvm_push_bool(vm, g_gui.mouse_clicked);
    g_gui.mouse_clicked = 0;
    g_gui.mouse_click_consumed = 0;
    return 1;
}

static int gui_op_is_mouse_hover(HVM_GuiRuntime *gui, HVM_VM *vm, const HVM_Instruction *instr) {
    HVM_Value id;
    const char *id_name;
    HVM_WidgetState *ws;
    int hovered = 0;
    (void)instr;
    if (vm->stack_top < 1) { hvm_set_error_msg(vm, "Stack underflow in IS_MOUSE_HOVER"); vm->running = 0; return 1; }
    id = hvm_pop(vm);
    if (id.type == HVM_TYPE_STRING) id_name = id.data.string_value ? id.data.string_value : "";
    else id_name = hvm_value_to_cstring_a(vm, id);
    if (!id_name) {
        hvm_free_value(&id);
        hvm_set_error_msg(vm, "Out of memory in IS_MOUSE_HOVER");
        vm->running = 0;
        return 1;
    }
    ws = gui_find_widget_state(gui, id_name);
    if (ws && ws->frame_seen == g_gui.frame_index) {
        hovered = gui_point_in_rect(gui, g_gui.mouse_x, g_gui.mouse_y, ws->x, ws->y, ws->w, ws->h);
    }
    hvm_push_bool(vm, hovered);
    hvm_free_value(&id);
    return 1;
}

static int gui_op_is_key_down(HVM_GuiRuntime *gui, HVM_VM *vm, const HVM_Instruction *instr) {
    HVM_Value key;
    int code;
    (void)gui; (void)instr;
    if (vm->stack_top < 1) { hvm_set_error_msg(vm, "Stack underflow in IS_KEY_DOWN"); vm->running = 0; return 1; }
    key = hvm_pop(vm);
    code = (key.type == HVM_TYPE_FLOAT) ? (int)key.data.float_value : (int)key.data.int_value;
    if (code < 0 || code >= 256) code = 0;
    hvm_push_bool(vm, g_gui.key_down[code & 0xFF]);
    hvm_free_value(&key);
    return 1;
}

static int gui_op_was_key_press(HVM_GuiRuntime *gui, HVM_VM *vm, const HVM_Instruction *instr) {
    HVM_Value key;
    int code;
    int pressed;
    (void)gui; (void)instr;
    if (vm->stack_top < 1) { hvm_set_error_msg(vm, "Stack underflow in WAS_KEY_PRESS"); vm->running = 0; return 1; }
    key = hvm_pop(vm);
    code = (key.type == HVM_TYPE_FLOAT) ? (int)key.data.float_value : (int)key.data.int_value;
    if (code < 0 || code >= 256) code = 0;
    pressed = g_gui.key_pressed[code & 0xFF];
    g_gui.key_pressed[code & 0xFF] = 0;
    hvm_push_bool(vm, pressed);
    hvm_free_value(&key);
    return 1;
}

static int gui_op_delta_time(HVM_GuiRuntime *gui, HVM_VM *vm, const HVM_Instruction *instr) {
    (void)gui; (void)instr;
    hvm_push_float(vm, (double)g_gui.delta_ms);
    return 1;
}

static int gui_op_layout_reset(HVM_GuiRuntime *gui, HVM_VM *vm, const HVM_Instruction *instr) {
    HVM_Value gap, y, x;
    (void)instr;
    if (vm->stack_top < 3) { hvm_set_error_msg(vm, "Stack underflow in LAYOUT_RESET"); vm->running = 0; return 1; }
    gap = hvm_pop(vm);
    y = hvm_pop(vm);
    x = hvm_pop(vm);

    g_gui.layout_x = (int)((x.type == HVM_TYPE_FLOAT) ? x.data.float_value : x.data.int_value);
    g_gui.layout_y = (int)((y.type == HVM_TYPE_FLOAT) ? y.data.float_value : y.data.int_value);
    g_gui.layout_base_x = g_gui.layout_x;
    g_gui.layout_base_y = g_gui.layout_y;
    g_gui.layout_gap = (int)((gap.type == HVM_TYPE_FLOAT) ? gap.data.float_value : gap.data.int_value);
    if (g_gui.layout_gap < 0) g_gui.layout_gap = 0;

    g_gui.layout_mode = GUI_LAYOUT_FLOW;
    g_gui.layout_index = 0;

    hvm_push_int(vm, 0);
    hvm_free_value(&gap);
    hvm_free_value(&y);
    hvm_free_value(&x);
    return 1;
}

static int gui_op_layout_next(HVM_GuiRuntime *gui, HVM_VM *vm, const HVM_Instruction *instr) {
    HVM_Value h;
    int height;
    int y;
    (void)gui; (void)instr;
    if (vm->stack_top < 1) { hvm_set_error_msg(vm, "Stack underflow in LAYOUT_NEXT"); vm->running = 0; return 1; }
    h = hvm_pop(vm);
    height = (h.type == HVM_TYPE_FLOAT) ? (int)h.data.float_value : (int)h.data.int_value;
    if (height < 0) height = 0;

    switch ((HVM_GuiLayoutMode)g_gui.layout_mode) {
        case GUI_LAYOUT_ROW_MODE: {
            int cols = g_gui.layout_cols > 0 ? g_gui.layout_cols : 1;
            int row_h = g_gui.layout_row_height > 0 ? g_gui.layout_row_height : height;
            int next_row;
            g_gui.layout_index = ((g_gui.layout_index / cols) + 1) * cols;
            next_row = g_gui.layout_index / cols;
            g_gui.layout_y = g_gui.layout_base_y + next_row * (row_h + g_gui.layout_gap);
            break;
        }
        case GUI_LAYOUT_GRID_MODE: {
            int cols = g_gui.layout_cols > 0 ? g_gui.layout_cols : 2;
            int cell_h = g_gui.layout_grid_cell_h > 0 ? g_gui.layout_grid_cell_h : height;
            int next_row;
            g_gui.layout_index = ((g_gui.layout_index / cols) + 1) * cols;
            next_row = g_gui.layout_index / cols;
            g_gui.layout_y = g_gui.layout_base_y + next_row * (cell_h + g_gui.layout_gap);
            break;
        }
        case GUI_LAYOUT_COLUMN_MODE:
        case GUI_LAYOUT_FLOW:
        default:
            g_gui.layout_y += height + g_gui.layout_gap;
            g_gui.layout_index++;
            break;
    }

    y = g_gui.layout_y;
    hvm_push_int(vm, y);
    hvm_free_value(&h);
    return 1;
}

static int gui_op_layout_row(HVM_GuiRuntime *gui, HVM_VM *vm, const HVM_Instruction *instr) {
    HVM_Value cols_value;
    int cols;
    (void)gui; (void)instr;
    if (vm->stack_top < 1) { hvm_set_error_msg(vm, "Stack underflow in LAYOUT_ROW"); vm->running = 0; return 1; }
    cols_value = hvm_pop(vm);
    cols = (cols_value.type == HVM_TYPE_FLOAT) ? (int)cols_value.data.float_value : (int)cols_value.data.int_value;
    if (cols < 1) cols = 1;
    g_gui.layout_mode = GUI_LAYOUT_ROW_MODE;
    g_gui.layout_cols = cols;
    if (g_gui.layout_col_width < 1) g_gui.layout_col_width = 180;
    if (g_gui.layout_row_height < 1) g_gui.layout_row_height = 32;
    g_gui.layout_base_y = g_gui.layout_y;
    g_gui.layout_x = g_gui.layout_base_x;
    g_gui.layout_index = 0;
    hvm_push_int(vm, cols);
    hvm_free_value(&cols_value);
    return 1;
}

static int gui_op_layout_column(HVM_GuiRuntime *gui, HVM_VM *vm, const HVM_Instruction *instr) {
    HVM_Value width_value;
    int width;
    (void)gui; (void)instr;
    if (vm->stack_top < 1) { hvm_set_error_msg(vm, "Stack underflow in LAYOUT_COLUMN"); vm->running = 0; return 1; }
    width_value = hvm_pop(vm);
    width = (width_value.type == HVM_TYPE_FLOAT) ? (int)width_value.data.float_value : (int)width_value.data.int_value;
    if (width < 40) width = 40;
    g_gui.layout_mode = GUI_LAYOUT_COLUMN_MODE;
    g_gui.layout_col_width = width;
    g_gui.layout_base_y = g_gui.layout_y;
    g_gui.layout_x = g_gui.layout_base_x;
    g_gui.layout_index = 0;
    hvm_push_int(vm, width);
    hvm_free_value(&width_value);
    return 1;
}

static int gui_op_layout_grid(HVM_GuiRuntime *gui, HVM_VM *vm, const HVM_Instruction *instr) {
    HVM_Value cell_h_value, cell_w_value, cols_value;
    int cols;
    int cell_w;
    int cell_h;
    (void)gui; (void)instr;
    if (vm->stack_top < 3) { hvm_set_error_msg(vm, "Stack underflow in LAYOUT_GRID"); vm->running = 0; return 1; }
    cell_h_value = hvm_pop(vm);
    cell_w_value = hvm_pop(vm);
    cols_value = hvm_pop(vm);
    cols = (cols_value.type == HVM_TYPE_FLOAT) ? (int)cols_value.data.float_value : (int)cols_value.data.int_value;
    cell_w = (cell_w_value.type == HVM_TYPE_FLOAT) ? (int)cell_w_value.data.float_value : (int)cell_w_value.data.int_value;
    cell_h = (cell_h_value.type == HVM_TYPE_FLOAT) ? (int)cell_h_value.data.float_value : (int)cell_h_value.data.int_value;
    if (cols < 1) cols = 1;
    if (cell_w < 40) cell_w = 40;
    if (cell_h < 20) cell_h = 20;
    g_gui.layout_mode = GUI_LAYOUT_GRID_MODE;
    g_gui.layout_cols = cols;
    g_gui.layout_grid_cell_w = cell_w;
    g_gui.layout_grid_cell_h = cell_h;
    g_gui.layout_base_y = g_gui.layout_y;
    g_gui.layout_x = g_gui.layout_base_x;
    g_gui.layout_index = 0;
    hvm_push_int(vm, cols);
    hvm_free_value(&cell_h_value);
    hvm_free_value(&cell_w_value);
    hvm_free_value(&cols_value);
    return 1;
}

static int gui_op_loop(HVM_GuiRuntime *gui, HVM_VM *vm, const HVM_Instruction *instr) {
    double now = gui_now_ms();
    (void)instr;
    g_gui.delta_ms = (g_gui.last_tick == 0) ? 0.0 : (now - (double)g_gui.last_tick);
    g_gui.last_tick = (uint64_t)now;
    g_gui.loop_called = 1;

    gui_reset_transient_input(gui);

    if (g_gui.using_real && !g_gui.running) {
        vm->running = 0;
    } else if (g_gui.using_real && g_gui.running) {
        gui_pump_events(gui);
        gui_apply_click_focus_from_commands(gui);
        gui_request_repaint(gui);
        g_gui.layout_x = g_gui.layout_base_x;
        g_gui.layout_y = g_gui.layout_base_y;
        g_gui.layout_index = 0;
        if (g_gui.frame_index == 0) g_gui.frame_index = 1;
        else g_gui.frame_index++;
    } else if (!g_gui.using_real) {
        if (g_gui.frame_index == 0) g_gui.frame_index = 1;
        else g_gui.frame_index++;
        GUI_LOG("[GUI] loop tick\n");
    }
    hvm_push_float(vm, g_gui.delta_ms);
    return 1;
}

static int gui_op_menu_setup_notepad(HVM_GuiRuntime *gui, HVM_VM *vm, const HVM_Instruction *instr) {
    int ok = 0;
    (void)instr;
    if (g_gui.using_real && g_gui.running) {
        ok = gui_menu_setup_notepad(gui);
    }
    hvm_push_bool(vm, ok);
    return 1;
}

static int gui_op_menu_event(HVM_GuiRuntime *gui, HVM_VM *vm, const HVM_Instruction *instr) {
    (void)instr;
    if (g_gui.using_real && g_gui.running) gui_pump_events(gui);
    hvm_push_int(vm, gui_take_menu_event(gui));
    return 1;
}

static int gui_op_scroll_set_range(HVM_GuiRuntime *gui, HVM_VM *vm, const HVM_Instruction *instr) {
    HVM_Value total;
    int total_height;
    (void)instr;
    if (vm->stack_top < 1) { hvm_set_error_msg(vm, "Stack underflow in SCROLL_SET_RANGE"); vm->running = 0; return 1; }
    total = hvm_pop(vm);
    total_height = (total.type == HVM_TYPE_FLOAT) ? (int)total.data.float_value : (int)total.data.int_value;
    if (total_height < 0) total_height = 0;
    g_gui.scroll_range = total_height;
#ifdef _WIN32
    if (g_gui.using_real && g_gui.running) gui_update_scrollbar(gui);
#endif
    hvm_push_int(vm, g_gui.scroll_range);
    hvm_free_value(&total);
    return 1;
}

static int gui_op_scroll_y(HVM_GuiRuntime *gui, HVM_VM *vm, const HVM_Instruction *instr) {
    (void)gui; (void)instr;
    hvm_push_int(vm, g_gui.scroll_y);
    return 1;
}

static int gui_op_file_open_dialog(HVM_GuiRuntime *gui, HVM_VM *vm, const HVM_Instruction *instr) {
    char *path_value = NULL;
    (void)instr;
    if (g_gui.using_real && g_gui.running) path_value = gui_open_file_dialog(gui);
    if (!path_value) path_value = strdup("");
    if (!path_value || !hvm_push_string(vm, path_value)) {
        free(path_value);
        hvm_set_error_msg(vm, "Failed to push OPEN dialog path");
        vm->running = 0;
        return 1;
    }
    free(path_value);
    return 1;
}

static int gui_op_file_save_dialog(HVM_GuiRuntime *gui, HVM_VM *vm, const HVM_Instruction *instr) {
    char *path_value = NULL;
    (void)instr;
    if (g_gui.using_real && g_gui.running) path_value = gui_save_file_dialog(gui);
    if (!path_value) path_value = strdup("");
    if (!path_value || !hvm_push_string(vm, path_value)) {
        free(path_value);
        hvm_set_error_msg(vm, "Failed to push SAVE dialog path");
        vm->running = 0;
        return 1;
    }
    free(path_value);
    return 1;
}

static int gui_op_input_set(HVM_GuiRuntime *gui, HVM_VM *vm, const HVM_Instruction *instr) {
    HVM_Value value;
    HVM_Value label;
    const char *label_name = NULL;
    const char *text_value = NULL;
    HVM_InputState *st;
    int ok = 0;
    (void)instr;

    if (vm->stack_top < 2) { hvm_set_error_msg(vm, "Stack underflow in INPUT_SET"); vm->running = 0; return 1; }

    value = hvm_pop(vm);
    label = hvm_pop(vm);

    if (label.type == HVM_TYPE_STRING) label_name = label.data.string_value ? label.data.string_value : "";
    else label_name = hvm_value_to_cstring_a(vm, label);

    if (value.type == HVM_TYPE_STRING) text_value = value.data.string_value ? value.data.string_value : "";
    else text_value = hvm_value_to_cstring_b(vm, value);

    if (!label_name || !text_value) {
        hvm_free_value(&value);
        hvm_free_value(&label);
        hvm_set_error_msg(vm, "Out of memory in INPUT_SET");
        vm->running = 0;
        return 1;
    }

    st = gui_get_input_state(gui, label_name);
    if (st) {
        ok = gui_buffer_set(gui, &st->buffer, &st->len, &st->cap, text_value);
        if (!ok) {
            hvm_set_error_msg(vm, "Out of memory resizing INPUT_SET");
            vm->running = 0;
        }
    }

    if (!hvm_push_bool(vm, ok)) {
        hvm_set_error_msg(vm, "Stack overflow in INPUT_SET");
        vm->running = 0;
    }

    hvm_free_value(&value);
    hvm_free_value(&label);
    return 1;
}

static int gui_op_textarea_set(HVM_GuiRuntime *gui, HVM_VM *vm, const HVM_Instruction *instr) {
    HVM_Value value;
    HVM_Value id;
    const char *id_name = NULL;
    const char *text_value = NULL;
    HVM_TextAreaState *ta;
    int ok = 0;
    (void)instr;

    if (vm->stack_top < 2) { hvm_set_error_msg(vm, "Stack underflow in TEXTAREA_SET"); vm->running = 0; return 1; }

    value = hvm_pop(vm);
    id = hvm_pop(vm);

    if (id.type == HVM_TYPE_STRING) id_name = id.data.string_value ? id.data.string_value : "";
    else id_name = hvm_value_to_cstring_a(vm, id);

    if (value.type == HVM_TYPE_STRING) text_value = value.data.string_value ? value.data.string_value : "";
    else text_value = hvm_value_to_cstring_b(vm, value);

    if (!id_name || !text_value) {
        hvm_free_value(&value);
        hvm_free_value(&id);
        hvm_set_error_msg(vm, "Out of memory in TEXTAREA_SET");
        vm->running = 0;
        return 1;
    }

    ta = gui_get_textarea_state(gui, id_name);
    if (ta) {
        ok = gui_buffer_set(gui, &ta->buffer, &ta->len, &ta->cap, text_value);
        if (!ok) {
            hvm_set_error_msg(vm, "Out of memory resizing TEXTAREA_SET");
            vm->running = 0;
        }
    }

    if (!hvm_push_bool(vm, ok)) {
        hvm_set_error_msg(vm, "Stack overflow in TEXTAREA_SET");
        vm->running = 0;
    }

    hvm_free_value(&value);
    hvm_free_value(&id);
    return 1;
}

static const HVM_GuiOpcodeHandler g_gui_handlers[HVM_OPCODE_COUNT] = {
    [HVM_CREATE_WINDOW] = gui_op_create_window,
    [HVM_CLEAR] = gui_op_clear,
    [HVM_SET_BG_COLOR] = gui_op_set_bg_color,
    [HVM_SET_COLOR] = gui_op_set_color,
    [HVM_SET_FONT_SIZE] = gui_op_set_font_size,
    [HVM_DRAW_TEXT] = gui_op_draw_text,
    [HVM_DRAW_BUTTON] = gui_op_draw_button,
    [HVM_DRAW_BUTTON_STATE] = gui_op_draw_button_state,
    [HVM_DRAW_INPUT] = gui_op_draw_input,
    [HVM_DRAW_INPUT_STATE] = gui_op_draw_input_state,
    [HVM_DRAW_TEXTAREA] = gui_op_draw_textarea,
    [HVM_DRAW_IMAGE] = gui_op_draw_image,
    [HVM_GET_MOUSE_X] = gui_op_get_mouse_x,
    [HVM_GET_MOUSE_Y] = gui_op_get_mouse_y,
    [HVM_IS_MOUSE_DOWN] = gui_op_is_mouse_down,
    [HVM_WAS_MOUSE_UP] = gui_op_was_mouse_up,
    [HVM_WAS_MOUSE_CLICK] = gui_op_was_mouse_click,
    [HVM_IS_MOUSE_HOVER] = gui_op_is_mouse_hover,
    [HVM_IS_KEY_DOWN] = gui_op_is_key_down,
    [HVM_WAS_KEY_PRESS] = gui_op_was_key_press,
    [HVM_DELTA_TIME] = gui_op_delta_time,
    [HVM_LAYOUT_RESET] = gui_op_layout_reset,
    [HVM_LAYOUT_NEXT] = gui_op_layout_next,
    [HVM_LAYOUT_ROW] = gui_op_layout_row,
    [HVM_LAYOUT_COLUMN] = gui_op_layout_column,
    [HVM_LAYOUT_GRID] = gui_op_layout_grid,
    [HVM_LOOP] = gui_op_loop,
    [HVM_MENU_SETUP_NOTEPAD] = gui_op_menu_setup_notepad,
    [HVM_MENU_EVENT] = gui_op_menu_event,
    [HVM_SCROLL_SET_RANGE] = gui_op_scroll_set_range,
    [HVM_SCROLL_Y] = gui_op_scroll_y,
    [HVM_FILE_OPEN_DIALOG] = gui_op_file_open_dialog,
    [HVM_FILE_SAVE_DIALOG] = gui_op_file_save_dialog,
    [HVM_INPUT_SET] = gui_op_input_set,
    [HVM_TEXTAREA_SET] = gui_op_textarea_set
};

HVM_GuiRuntime* hvm_gui_create(void) {
    HVM_GuiRuntime *gui = (HVM_GuiRuntime *)calloc(1, sizeof(HVM_GuiRuntime));
    if (!gui) return NULL;
    gui_reset_style_defaults(gui);
    g_gui.last_tick = (uint64_t)gui_now_ms();
    g_gui.frame_index = 1;
    g_gui.using_real = 0;
    g_gui.running = 0;
    g_gui.loop_called = 0;
    g_gui.class_registered = 0;
    return gui;
}

void hvm_gui_destroy(HVM_GuiRuntime* gui) {
    if (!gui) return;
    gui_shutdown(gui);
    free(gui);
}

void hvm_gui_reset_for_run(HVM_GuiRuntime* gui) {
    if (!gui) return;
    g_gui.loop_called = 0;
    if (g_gui.frame_index == 0) g_gui.frame_index = 1;
    gui_reset_transient_input(gui);
}

int hvm_gui_handle_opcode(HVM_GuiRuntime* gui, HVM_VM* vm, HVM_Opcode opcode, const HVM_Instruction* instr) {
    HVM_GuiOpcodeHandler handler;
    if (!gui || !vm) return 0;
    if ((int)opcode < 0 || opcode >= HVM_OPCODE_COUNT) return 0;
    handler = g_gui_handlers[opcode];
    if (!handler) return 0;
    return handler(gui, vm, instr);
}

void hvm_gui_post_run(HVM_GuiRuntime* gui, HVM_VM* vm) {
    if (!gui || !vm) return;
    if (!vm->error_message && g_gui.using_real && g_gui.running) {
        if (!g_gui.loop_called) {
            GUI_LOG("[GUI] Close the window to exit.\n");
        }
        gui_run_loop_until_close(gui);
    }
}

