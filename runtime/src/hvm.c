#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "hvm.h"

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <windowsx.h>
#include <commdlg.h>
#endif

#ifndef _WIN32
#define RGB(r,g,b) (((unsigned int)(r) & 0xFF) | (((unsigned int)(g) & 0xFF) << 8) | (((unsigned int)(b) & 0xFF) << 16))
#endif

#define HVM_MAX_GLOBALS 512
#define HVM_MAX_INSTRUCTIONS 65536
#define HVM_GC_INITIAL_THRESHOLD (64u * 1024u)
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

typedef struct {
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
} HVM_GuiState;

static HVM_GuiState g_gui = {0};
static HVM_GCObject* hvm_gc_find_object(HVM_VM* vm, const char* ptr) {
    HVM_GCObject *it;
    if (!vm || !ptr) return NULL;
    it = vm->gc_objects;
    while (it) {
        if (it->ptr == ptr) return it;
        it = it->next;
    }
    return NULL;
}

static int hvm_gc_track_string(HVM_VM* vm, char* ptr, size_t size) {
    HVM_GCObject *obj;
    if (!vm || !ptr) return 0;

    obj = (HVM_GCObject *)malloc(sizeof(HVM_GCObject));
    if (!obj) return 0;

    obj->ptr = ptr;
    obj->size = size;
    obj->marked = 0;
    obj->next = vm->gc_objects;
    vm->gc_objects = obj;
    vm->gc_object_count++;
    vm->gc_bytes += size;

    if (vm->gc_enabled && vm->gc_bytes >= vm->gc_next_collection) {
        vm->gc_pending = 1;
    }

    return 1;
}

static char* hvm_gc_strdup(HVM_VM* vm, const char* value) {
    size_t len;
    char *ptr;

    if (!vm) return NULL;
    if (!value) value = "";

    len = strlen(value) + 1;
    ptr = strdup(value);
    if (!ptr) return NULL;

    if (!hvm_gc_track_string(vm, ptr, len)) {
        free(ptr);
        return NULL;
    }

    return ptr;
}

static void hvm_gc_mark_pointer(HVM_VM* vm, const char* ptr) {
    HVM_GCObject *obj;
    if (!vm || !ptr) return;
    obj = hvm_gc_find_object(vm, ptr);
    if (obj) obj->marked = 1;
}

static void hvm_gc_mark_roots(HVM_VM* vm) {
    size_t i;
    if (!vm) return;

    for (i = 0; i < vm->stack_top; i++) {
        if (vm->stack[i].type == HVM_TYPE_STRING && vm->stack[i].data.string_value) {
            hvm_gc_mark_pointer(vm, vm->stack[i].data.string_value);
        }
    }

    for (i = 0; i < vm->memory_used; i++) {
        if (vm->memory[i].type == HVM_TYPE_STRING && vm->memory[i].data.string_value) {
            hvm_gc_mark_pointer(vm, vm->memory[i].data.string_value);
        }
    }
}

static void hvm_gc_sweep(HVM_VM* vm) {
    HVM_GCObject *cur;
    HVM_GCObject *prev;

    if (!vm) return;

    prev = NULL;
    cur = vm->gc_objects;
    while (cur) {
        if (!cur->marked) {
            HVM_GCObject *dead = cur;
            if (prev) prev->next = cur->next;
            else vm->gc_objects = cur->next;

            cur = cur->next;
            vm->gc_object_count--;
            if (vm->gc_bytes >= dead->size) vm->gc_bytes -= dead->size;
            else vm->gc_bytes = 0;

            free(dead->ptr);
            free(dead);
        } else {
            prev = cur;
            cur = cur->next;
        }
    }
}

static void hvm_gc_collect_internal(HVM_VM* vm) {
    HVM_GCObject *it;
    size_t next;

    if (!vm || !vm->gc_enabled) return;

    it = vm->gc_objects;
    while (it) {
        it->marked = 0;
        it = it->next;
    }

    hvm_gc_mark_roots(vm);
    hvm_gc_sweep(vm);

    vm->gc_pending = 0;

    next = vm->gc_bytes * 2;
    if (next < HVM_GC_INITIAL_THRESHOLD) next = HVM_GC_INITIAL_THRESHOLD;
    vm->gc_next_collection = next;
}

static void hvm_gc_destroy_all(HVM_VM* vm) {
    HVM_GCObject *it;
    if (!vm) return;

    it = vm->gc_objects;
    while (it) {
        HVM_GCObject *next = it->next;
        free(it->ptr);
        free(it);
        it = next;
    }

    vm->gc_objects = NULL;
    vm->gc_object_count = 0;
    vm->gc_bytes = 0;
    vm->gc_pending = 0;
}

static int gui_debug_enabled(void) {
    const char *env = getenv("HVM_GUI_DEBUG");
    if (!env || env[0] == '\0') return 0;
    if (strcmp(env, "1") == 0 || strcmp(env, "true") == 0 || strcmp(env, "yes") == 0) return 1;
    return 0;
}

#define GUI_LOG(...) do { if (!g_gui.using_real || gui_debug_enabled()) printf(__VA_ARGS__); } while (0)

static void gui_free_commands(void) {
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

static void gui_clear_commands(void) {
    size_t i;
    for (i = 0; i < g_gui.cmd_count; i++) {
        free(g_gui.cmds[i].text);
        g_gui.cmds[i].text = NULL;
    }
    g_gui.cmd_count = 0;
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

static void gui_free_inputs(void) {
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

static void gui_free_textareas(void) {
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

static void gui_free_widgets(void) {
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

static HVM_WidgetState* gui_find_widget_state(const char *id) {
    size_t i;
    if (!id) id = "";
    for (i = 0; i < g_gui.widget_count; i++) {
        if (g_gui.widgets[i].id && strcmp(g_gui.widgets[i].id, id) == 0) {
            return &g_gui.widgets[i];
        }
    }
    return NULL;
}

static HVM_WidgetState* gui_get_widget_state(const char *id) {
    HVM_WidgetState *state;
    if (!id) id = "";

    state = gui_find_widget_state(id);
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

static int gui_point_in_rect(int px, int py, int x, int y, int w, int h) {
    if (w < 0) w = 0;
    if (h < 0) h = 0;
    return px >= x && py >= y && px <= x + w && py <= y + h;
}

static int gui_take_mouse_click_in_rect(int x, int y, int w, int h) {
    if (!g_gui.mouse_clicked || g_gui.mouse_click_consumed) return 0;
    if (!gui_point_in_rect(g_gui.mouse_click_x, g_gui.mouse_click_y, x, y, w, h)) return 0;
    g_gui.mouse_click_consumed = 1;
    return 1;
}

static int gui_take_mouse_focus_in_rect(int x, int y, int w, int h) {
    if (gui_take_mouse_click_in_rect(x, y, w, h)) return 1;
    if (g_gui.mouse_down && gui_point_in_rect(g_gui.mouse_x, g_gui.mouse_y, x, y, w, h)) {
        return 1;
    }
    return 0;
}

static void gui_reset_transient_input(void) {
    g_gui.mouse_up = 0;
    g_gui.mouse_clicked = 0;
    g_gui.mouse_click_consumed = 0;
    memset(g_gui.key_pressed, 0, sizeof(g_gui.key_pressed));
}

static void gui_apply_click_focus_from_commands(void) {
    size_t i;

    if (!g_gui.mouse_clicked || g_gui.mouse_click_consumed) {
        return;
    }

    for (i = g_gui.cmd_count; i > 0; i--) {
        HVM_GuiCmd *cmd = &g_gui.cmds[i - 1];

        if (!gui_point_in_rect(g_gui.mouse_click_x, g_gui.mouse_click_y, cmd->x, cmd->y, cmd->w, cmd->h)) {
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
static HVM_WidgetState* gui_register_widget_rect(const char *id, int x, int y, int w, int h) {
    HVM_WidgetState *state = gui_get_widget_state(id);
    if (!state) return NULL;

    state->x = x;
    state->y = y;
    state->w = w;
    state->h = h;
    state->hovered = gui_point_in_rect(g_gui.mouse_x, g_gui.mouse_y, x, y, w, h);
    state->down = state->hovered && g_gui.mouse_down;
    state->clicked = gui_point_in_rect(g_gui.mouse_click_x, g_gui.mouse_click_y, x, y, w, h) && g_gui.mouse_clicked;
    state->frame_seen = g_gui.frame_index;
    return state;
}

static void gui_layout_place_widget(int pref_w, int pref_h, int *x, int *y, int *w, int *h) {
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
static HVM_InputState* gui_get_input_state(const char *label) {
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

static HVM_TextAreaState* gui_get_textarea_state(const char *id) {
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

static int gui_push_cmd(const HVM_GuiCmd *cmd) {
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

static void gui_reset_style_defaults(void) {
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
static int g_gui_class_registered = 0;

static int gui_headless_mode(void) {
    const char *env = getenv("HVM_GUI_HEADLESS");
    if (!env || env[0] == '\0') return 0;
    if (strcmp(env, "1") == 0 || strcmp(env, "true") == 0 || strcmp(env, "yes") == 0) return 1;
    return 0;
}

static void gui_update_scrollbar(void) {
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

static void gui_scroll_to(int new_pos) {
    g_gui.scroll_y = new_pos;
    gui_update_scrollbar();
}

static void gui_handle_vscroll(WPARAM wparam) {
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
        gui_scroll_to(pos);
        InvalidateRect(g_gui.hwnd, NULL, FALSE);
    }
}

static int gui_menu_setup_notepad(void) {
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

static int gui_take_menu_event(void) {
    int cmd = g_gui.last_menu_cmd;
    g_gui.last_menu_cmd = 0;
    return cmd;
}

static char *gui_file_dialog(int save_mode) {
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

static char *gui_open_file_dialog(void) {
    return gui_file_dialog(0);
}

static char *gui_save_file_dialog(void) {
    return gui_file_dialog(1);
}

static LRESULT CALLBACK hvm_gui_wndproc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam) {
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
            gui_update_scrollbar();
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
            gui_handle_vscroll(wparam);
            return 0;
        case WM_MOUSEWHEEL: {
            int delta = GET_WHEEL_DELTA_WPARAM(wparam);
            int step = (delta / WHEEL_DELTA) * 40;
            if (step != 0) {
                gui_scroll_to(g_gui.scroll_y - step);
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
                    if (!gui_buffer_append_char(&ta->buffer, &ta->len, &ta->cap, '\n')) return 0;
                } else if (wparam == 9) {
                    if (!gui_buffer_append_char(&ta->buffer, &ta->len, &ta->cap, ' ')) return 0;
                    if (!gui_buffer_append_char(&ta->buffer, &ta->len, &ta->cap, ' ')) return 0;
                    if (!gui_buffer_append_char(&ta->buffer, &ta->len, &ta->cap, ' ')) return 0;
                    if (!gui_buffer_append_char(&ta->buffer, &ta->len, &ta->cap, ' ')) return 0;
                } else if (wparam >= 32 && wparam < 127) {
                    if (!gui_buffer_append_char(&ta->buffer, &ta->len, &ta->cap, (char)wparam)) return 0;
                }
                if (g_gui.hwnd) InvalidateRect(g_gui.hwnd, NULL, FALSE);
                return 0;
            }

            if (g_gui.active_input != GUI_NO_ACTIVE_INPUT && g_gui.active_input < g_gui.input_count) {
                HVM_InputState *st = &g_gui.inputs[g_gui.active_input];
                if (wparam == 8) {
                    if (st->len > 0) st->buffer[--st->len] = '\0';
                } else if (wparam >= 32 && wparam < 127) {
                    if (!gui_buffer_append_char(&st->buffer, &st->len, &st->cap, (char)wparam)) return 0;
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
                        int hovered = gui_point_in_rect(g_gui.mouse_x, g_gui.mouse_y, cmd->x, cmd->y, cmd->w, cmd->h);
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

static int gui_register_class(void) {
    WNDCLASSA wc;
    if (g_gui_class_registered) return 1;

    memset(&wc, 0, sizeof(wc));
    wc.lpfnWndProc = hvm_gui_wndproc;
    wc.hInstance = GetModuleHandleA(NULL);
    wc.lpszClassName = HVM_GUI_CLASS_NAME;
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);
    wc.hbrBackground = NULL;

    if (!RegisterClassA(&wc)) {
        return 0;
    }

    g_gui_class_registered = 1;
    return 1;
}

static void gui_pump_events(void) {
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

static void gui_request_repaint(void) {
    if (!g_gui.hwnd) return;
    InvalidateRect(g_gui.hwnd, NULL, FALSE);
    /* paint is handled by message loop */
}

static int gui_create_window(const char *title) {
    if (gui_headless_mode()) return 0;
    if (!gui_register_class()) return 0;

    while (1) {
        MSG msg;
        if (!PeekMessageA(&msg, NULL, WM_QUIT, WM_QUIT, PM_REMOVE)) break;
    }

    if (g_gui.hwnd) {
        DestroyWindow(g_gui.hwnd);
        g_gui.hwnd = NULL;
    }

    gui_free_commands();
    gui_free_inputs();
    gui_free_textareas();
    gui_free_widgets();
    gui_reset_style_defaults();
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

    ShowWindow(g_gui.hwnd, SW_SHOW);
    gui_update_scrollbar();
    /* paint is handled by message loop */
    g_gui.using_real = 1;
    g_gui.running = 1;
    return 1;
}

static void gui_run_loop_until_close(void) {
    while (g_gui.running && g_gui.hwnd) {
        gui_reset_transient_input();
        gui_pump_events();
        if (!g_gui.running || !g_gui.hwnd) break;

        gui_apply_click_focus_from_commands();
        gui_request_repaint();
        Sleep(16);
    }
}
#else
static int gui_headless_mode(void) { return 1; }
static void gui_pump_events(void) { }
static void gui_request_repaint(void) { }
static int gui_create_window(const char *title) { (void)title; return 0; }
static void gui_run_loop_until_close(void) { }
static int gui_menu_setup_notepad(void) { return 0; }
static int gui_take_menu_event(void) { return 0; }
static char *gui_open_file_dialog(void) { return NULL; }
static char *gui_save_file_dialog(void) { return NULL; }
#endif

static void gui_shutdown(void) {
#ifdef _WIN32
    if (g_gui.hwnd) {
        DestroyWindow(g_gui.hwnd);
        g_gui.hwnd = NULL;
    }
#endif
    gui_free_commands();
    gui_free_inputs();
    gui_free_textareas();
    gui_free_widgets();
    gui_reset_style_defaults();
    g_gui.last_tick = (uint64_t)gui_now_ms();
    g_gui.frame_index = 1;
    g_gui.using_real = 0;
    g_gui.running = 0;
    g_gui.loop_called = 0;
}
static int opcode_uses_string_operand(HVM_Opcode opcode) {
    return opcode == HVM_PUSH_STRING ||
           opcode == HVM_STORE_GLOBAL ||
           opcode == HVM_LOAD_GLOBAL ||
           opcode == HVM_CREATE_WINDOW;
}

static void hvm_free_value(HVM_Value* v) {
    if (!v) return;
    if (v->type == HVM_TYPE_STRING) {
        v->data.string_value = NULL;
    }
    v->type = HVM_TYPE_NULL;
}

static void hvm_set_error_msg(HVM_VM* vm, const char* msg) {
    if (!vm) return;
    if (vm->error_message) free(vm->error_message);
    vm->error_message = msg ? strdup(msg) : NULL;
}

static void hvm_clear_instructions(HVM_VM* vm) {
    size_t i;
    if (!vm || !vm->instructions) return;
    for (i = 0; i < vm->instruction_count; i++) {
        if (opcode_uses_string_operand(vm->instructions[i].opcode) && vm->instructions[i].operand.string_operand) {
            free(vm->instructions[i].operand.string_operand);
            vm->instructions[i].operand.string_operand = NULL;
        }
    }
    free(vm->instructions);
    vm->instructions = NULL;
    vm->instruction_count = 0;
    vm->instruction_capacity = 0;
    vm->pc = 0;
}

static int hvm_is_truthy(HVM_Value v) {
    switch (v.type) {
        case HVM_TYPE_BOOL: return v.data.bool_value != 0;
        case HVM_TYPE_INT: return v.data.int_value != 0;
        case HVM_TYPE_FLOAT: return v.data.float_value != 0.0;
        case HVM_TYPE_STRING: return v.data.string_value && v.data.string_value[0] != '\0';
        default: return 0;
    }
}

static int hvm_is_numeric(HVM_Value v) {
    return v.type == HVM_TYPE_INT || v.type == HVM_TYPE_FLOAT || v.type == HVM_TYPE_BOOL;
}

static double hvm_to_double(HVM_Value v) {
    if (v.type == HVM_TYPE_FLOAT) return v.data.float_value;
    if (v.type == HVM_TYPE_BOOL) return v.data.bool_value ? 1.0 : 0.0;
    return (double)v.data.int_value;
}

static char* hvm_value_to_string(HVM_Value v) {
    char buf[128];
    if (v.type == HVM_TYPE_STRING) return strdup(v.data.string_value ? v.data.string_value : "");
    if (v.type == HVM_TYPE_FLOAT) {
        snprintf(buf, sizeof(buf), "%g", v.data.float_value);
        return strdup(buf);
    }
    if (v.type == HVM_TYPE_BOOL) return strdup(v.data.bool_value ? "true" : "false");
    if (v.type == HVM_TYPE_INT) {
        snprintf(buf, sizeof(buf), "%lld", (long long)v.data.int_value);
        return strdup(buf);
    }
    return strdup("null");
}

static char *hvm_read_text_file(const char *path) {
    FILE *fp;
    long size;
    size_t read_size;
    char *buffer;

    if (!path || path[0] == '\0') return strdup("");

    fp = fopen(path, "rb");
    if (!fp) return strdup("");

    if (fseek(fp, 0, SEEK_END) != 0) {
        fclose(fp);
        return strdup("");
    }

    size = ftell(fp);
    if (size < 0) {
        fclose(fp);
        return strdup("");
    }

    if (fseek(fp, 0, SEEK_SET) != 0) {
        fclose(fp);
        return strdup("");
    }

    buffer = (char *)malloc((size_t)size + 1);
    if (!buffer) {
        fclose(fp);
        return strdup("");
    }

    read_size = fread(buffer, 1, (size_t)size, fp);
    buffer[read_size] = '\0';
    fclose(fp);
    return buffer;
}

static char *hvm_read_text_line(const char *path, int line_no) {
    char *content;
    char *start;
    char *p;
    char *line;
    size_t len;
    int current_line = 1;

    if (line_no < 1) line_no = 1;

    content = hvm_read_text_file(path);
    if (!content) return strdup("");

    start = content;
    p = content;
    while (*p && current_line < line_no) {
        if (*p == '\n') {
            current_line++;
            start = p + 1;
        }
        p++;
    }

    if (current_line != line_no) {
        free(content);
        return strdup("");
    }

    p = start;
    while (*p && *p != '\n' && *p != '\r') p++;

    len = (size_t)(p - start);
    line = (char *)malloc(len + 1);
    if (!line) {
        free(content);
        return strdup("");
    }

    memcpy(line, start, len);
    line[len] = '\0';
    free(content);
    return line;
}

static int hvm_write_text_file(const char *path, const char *text) {
    FILE *fp;
    size_t len;
    size_t written;

    if (!path || path[0] == '\0') return 0;

    fp = fopen(path, "wb");
    if (!fp) return 0;

    if (!text) text = "";
    len = strlen(text);
    written = fwrite(text, 1, len, fp);

    if (fclose(fp) != 0) return 0;
    return written == len;
}

static char *hvm_exec_command(const char *cmd) {
    FILE *pipe;
    char chunk[256];
    char *output;
    size_t len = 0;
    size_t cap = 1;

    if (!cmd || cmd[0] == '\0') return strdup("");

#ifdef _WIN32
    pipe = _popen(cmd, "r");
#else
    pipe = popen(cmd, "r");
#endif
    if (!pipe) return strdup("");

    output = (char *)malloc(cap);
    if (!output) {
#ifdef _WIN32
        _pclose(pipe);
#else
        pclose(pipe);
#endif
        return strdup("");
    }
    output[0] = '\0';

    while (fgets(chunk, (int)sizeof(chunk), pipe)) {
        size_t c_len = strlen(chunk);
        if (len + c_len + 1 > cap) {
            size_t new_cap = cap;
            char *n;
            while (len + c_len + 1 > new_cap) {
                new_cap *= 2;
            }
            n = (char *)realloc(output, new_cap);
            if (!n) {
                free(output);
#ifdef _WIN32
                _pclose(pipe);
#else
                pclose(pipe);
#endif
                return strdup("");
            }
            output = n;
            cap = new_cap;
        }
        memcpy(output + len, chunk, c_len + 1);
        len += c_len;
    }

#ifdef _WIN32
    _pclose(pipe);
#else
    pclose(pipe);
#endif
    return output;
}

HVM_VM* hvm_create(void) {
    HVM_VM *vm = (HVM_VM*)calloc(1, sizeof(HVM_VM));
    if (!vm) return NULL;

    vm->gc_enabled = 1;
    vm->gc_pending = 0;
    vm->gc_next_collection = HVM_GC_INITIAL_THRESHOLD;
    return vm;
}

void hvm_destroy(HVM_VM* vm) {
    size_t i;
    if (!vm) return;

    for (i = 0; i < vm->stack_top; i++) hvm_free_value(&vm->stack[i]);
    for (i = 0; i < vm->memory_used; i++) hvm_free_value(&vm->memory[i]);

    hvm_clear_instructions(vm);

    for (i = 0; i < vm->string_count; i++) {
        free(vm->strings[i]);
        vm->strings[i] = NULL;
    }

    hvm_gc_destroy_all(vm);

    if (vm->error_message) free(vm->error_message);
    gui_shutdown();
    free(vm);
}

int hvm_load_bytecode(HVM_VM* vm, const HVM_Instruction* instructions, size_t count) {
    size_t i;
    if (!vm || (!instructions && count > 0)) return 0;
    if (count > HVM_MAX_INSTRUCTIONS) return 0;

    hvm_clear_instructions(vm);

    if (count == 0) {
        vm->pc = 0;
        return 1;
    }

    vm->instructions = (HVM_Instruction*)calloc(count, sizeof(HVM_Instruction));
    if (!vm->instructions) return 0;

    for (i = 0; i < count; i++) {
        vm->instructions[i].opcode = instructions[i].opcode;
        if (opcode_uses_string_operand(instructions[i].opcode)) {
            if (instructions[i].operand.string_operand) {
                vm->instructions[i].operand.string_operand = strdup(instructions[i].operand.string_operand);
                if (!vm->instructions[i].operand.string_operand) {
                    hvm_clear_instructions(vm);
                    return 0;
                }
            } else {
                vm->instructions[i].operand.string_operand = NULL;
            }
        } else {
            vm->instructions[i].operand = instructions[i].operand;
        }
    }

    vm->instruction_count = count;
    vm->instruction_capacity = count;
    vm->pc = 0;
    return 1;
}

static int ensure_instruction_capacity(HVM_VM* vm) {
    size_t new_cap;
    HVM_Instruction* ni;

    if (vm->instruction_count < vm->instruction_capacity) return 1;
    if (vm->instruction_capacity >= HVM_MAX_INSTRUCTIONS) return 0;

    new_cap = vm->instruction_capacity ? vm->instruction_capacity * 2 : 16;
    if (new_cap > HVM_MAX_INSTRUCTIONS) new_cap = HVM_MAX_INSTRUCTIONS;

    ni = (HVM_Instruction*)realloc(vm->instructions, sizeof(HVM_Instruction) * new_cap);
    if (!ni) return 0;

    memset(ni + vm->instruction_capacity, 0, sizeof(HVM_Instruction) * (new_cap - vm->instruction_capacity));
    vm->instructions = ni;
    vm->instruction_capacity = new_cap;
    return 1;
}

int hvm_add_instruction(HVM_VM* vm, HVM_Opcode opcode, int64_t operand) {
    if (!vm || !ensure_instruction_capacity(vm)) return 0;
    vm->instructions[vm->instruction_count].opcode = opcode;
    vm->instructions[vm->instruction_count].operand.int_operand = operand;
    vm->instruction_count++;
    return 1;
}

int hvm_add_instruction_float(HVM_VM* vm, HVM_Opcode opcode, double operand) {
    if (!vm || !ensure_instruction_capacity(vm)) return 0;
    vm->instructions[vm->instruction_count].opcode = opcode;
    vm->instructions[vm->instruction_count].operand.float_operand = operand;
    vm->instruction_count++;
    return 1;
}

int hvm_add_instruction_string(HVM_VM* vm, HVM_Opcode opcode, const char* operand) {
    if (!vm || !operand || !ensure_instruction_capacity(vm)) return 0;
    vm->instructions[vm->instruction_count].opcode = opcode;
    vm->instructions[vm->instruction_count].operand.string_operand = strdup(operand);
    if (!vm->instructions[vm->instruction_count].operand.string_operand) return 0;
    vm->instruction_count++;
    return 1;
}

int hvm_add_instruction_address(HVM_VM* vm, HVM_Opcode opcode, size_t address) {
    if (!vm || !ensure_instruction_capacity(vm)) return 0;
    vm->instructions[vm->instruction_count].opcode = opcode;
    vm->instructions[vm->instruction_count].operand.address_operand = address;
    vm->instruction_count++;
    return 1;
}

int hvm_push_int(HVM_VM* vm, int64_t value) {
    if (!vm || vm->stack_top >= HVM_STACK_SIZE) return 0;
    vm->stack[vm->stack_top].type = HVM_TYPE_INT;
    vm->stack[vm->stack_top].data.int_value = value;
    vm->stack_top++;
    return 1;
}

int hvm_push_float(HVM_VM* vm, double value) {
    if (!vm || vm->stack_top >= HVM_STACK_SIZE) return 0;
    vm->stack[vm->stack_top].type = HVM_TYPE_FLOAT;
    vm->stack[vm->stack_top].data.float_value = value;
    vm->stack_top++;
    return 1;
}

int hvm_push_string(HVM_VM* vm, const char* value) {
    if (!vm || vm->stack_top >= HVM_STACK_SIZE || !value) return 0;
    vm->stack[vm->stack_top].type = HVM_TYPE_STRING;
    vm->stack[vm->stack_top].data.string_value = hvm_gc_strdup(vm, value);
    if (!vm->stack[vm->stack_top].data.string_value) return 0;
    vm->stack_top++;

    if (!vm->running && vm->gc_pending) {
        hvm_gc_collect_internal(vm);
    }

    return 1;
}

int hvm_push_bool(HVM_VM* vm, int value) {
    if (!vm || vm->stack_top >= HVM_STACK_SIZE) return 0;
    vm->stack[vm->stack_top].type = HVM_TYPE_BOOL;
    vm->stack[vm->stack_top].data.bool_value = value ? 1 : 0;
    vm->stack_top++;
    return 1;
}

HVM_Value hvm_pop(HVM_VM* vm) {
    HVM_Value r;
    memset(&r, 0, sizeof(r));
    r.type = HVM_TYPE_NULL;
    if (!vm || vm->stack_top == 0) {
        hvm_set_error_msg(vm, "Stack underflow");
        return r;
    }
    vm->stack_top--;
    r = vm->stack[vm->stack_top];
    vm->stack[vm->stack_top].type = HVM_TYPE_NULL;
    vm->stack[vm->stack_top].data.string_value = NULL;
    return r;
}

HVM_Value hvm_peek(HVM_VM* vm, size_t offset) {
    HVM_Value r;
    memset(&r, 0, sizeof(r));
    r.type = HVM_TYPE_NULL;
    if (!vm || vm->stack_top == 0 || offset >= vm->stack_top) {
        hvm_set_error_msg(vm, "Stack underflow");
        return r;
    }
    return vm->stack[vm->stack_top - 1 - offset];
}

static int find_global_index(HVM_VM* vm, const char* name) {
    size_t i;
    for (i = 0; i < vm->string_count; i++) {
        if (vm->strings[i] && strcmp(vm->strings[i], name) == 0) return (int)i;
    }
    return -1;
}

static char *resolve_runtime_name(HVM_VM* vm, const char* name) {
    int needed;
    char *out;
    if (!vm || !name) return NULL;
    if (strncmp(name, "__", 2) != 0 || vm->call_top == 0) return NULL;
    needed = snprintf(NULL, 0, "%s#%zu", name, vm->call_top);
    if (needed < 0) return NULL;
    out = (char *)malloc((size_t)needed + 1);
    if (!out) return NULL;
    snprintf(out, (size_t)needed + 1, "%s#%zu", name, vm->call_top);
    return out;
}

static int store_global(HVM_VM* vm, const char* name, HVM_Value value) {
    int idx;
    char *resolved_name;
    const char *key;

    if (!vm || !name) return 0;
    resolved_name = resolve_runtime_name(vm, name);
    key = resolved_name ? resolved_name : name;

    idx = find_global_index(vm, key);
    if (idx >= 0) {
        hvm_free_value(&vm->memory[idx]);
        vm->memory[idx] = value;
        free(resolved_name);
        return 1;
    }

    if (vm->string_count >= HVM_MAX_GLOBALS || vm->memory_used >= HVM_MEMORY_SIZE) {
        free(resolved_name);
        return 0;
    }

    vm->strings[vm->string_count] = strdup(key);
    if (!vm->strings[vm->string_count]) {
        free(resolved_name);
        return 0;
    }

    vm->memory[vm->string_count] = value;
    vm->string_count++;
    vm->memory_used++;
    free(resolved_name);
    return 1;
}

static int load_global(HVM_VM* vm, const char* name, HVM_Value* out) {
    int idx;
    char *resolved_name;
    const char *key;

    if (!vm || !name || !out) return 0;
    resolved_name = resolve_runtime_name(vm, name);
    key = resolved_name ? resolved_name : name;

    idx = find_global_index(vm, key);
    free(resolved_name);
    if (idx < 0) return 0;

    *out = vm->memory[idx];
    return 1;
}

int hvm_run(HVM_VM* vm) {
    if (!vm || !vm->instructions) return 0;

    if (vm->error_message) {
        free(vm->error_message);
        vm->error_message = NULL;
    }

    vm->running = 1;
    vm->pc = 0;
    g_gui.loop_called = 0;
    if (g_gui.frame_index == 0) g_gui.frame_index = 1;
    gui_reset_transient_input();

    while (vm->running && vm->pc < vm->instruction_count) {
        HVM_Instruction* instr = &vm->instructions[vm->pc];

        switch (instr->opcode) {
            case HVM_PUSH_INT:
                if (!hvm_push_int(vm, instr->operand.int_operand)) {
                    hvm_set_error_msg(vm, "Stack overflow");
                    vm->running = 0;
                }
                break;
            case HVM_PUSH_FLOAT:
                if (!hvm_push_float(vm, instr->operand.float_operand)) {
                    hvm_set_error_msg(vm, "Stack overflow");
                    vm->running = 0;
                }
                break;
            case HVM_PUSH_STRING:
                if (!hvm_push_string(vm, instr->operand.string_operand ? instr->operand.string_operand : "")) {
                    hvm_set_error_msg(vm, "Stack overflow");
                    vm->running = 0;
                }
                break;
            case HVM_PUSH_BOOL:
                if (!hvm_push_bool(vm, (int)instr->operand.int_operand)) {
                    hvm_set_error_msg(vm, "Stack overflow");
                    vm->running = 0;
                }
                break;

            case HVM_POP: {
                HVM_Value dropped;
                if (vm->stack_top < 1) {
                    hvm_set_error_msg(vm, "Stack underflow in POP");
                    vm->running = 0;
                    break;
                }
                dropped = hvm_pop(vm);
                hvm_free_value(&dropped);
                break;
            }

            case HVM_ADD: {
                HVM_Value a, b;
                if (vm->stack_top < 2) {
                    hvm_set_error_msg(vm, "Stack underflow in ADD");
                    vm->running = 0;
                    break;
                }
                b = hvm_pop(vm);
                a = hvm_pop(vm);

                if (a.type == HVM_TYPE_STRING || b.type == HVM_TYPE_STRING) {
                    char *sa = hvm_value_to_string(a);
                    char *sb = hvm_value_to_string(b);
                    char *out;
                    size_t lena, lenb;
                    if (!sa || !sb) {
                        free(sa);
                        free(sb);
                        hvm_set_error_msg(vm, "Out of memory in string concat");
                        vm->running = 0;
                        hvm_free_value(&a);
                        hvm_free_value(&b);
                        break;
                    }
                    lena = strlen(sa);
                    lenb = strlen(sb);
                    out = (char*)malloc(lena + lenb + 1);
                    if (!out) {
                        free(sa);
                        free(sb);
                        hvm_set_error_msg(vm, "Out of memory in string concat");
                        vm->running = 0;
                        hvm_free_value(&a);
                        hvm_free_value(&b);
                        break;
                    }
                    memcpy(out, sa, lena);
                    memcpy(out + lena, sb, lenb);
                    out[lena + lenb] = '\0';
                    if (!hvm_push_string(vm, out)) {
                        hvm_set_error_msg(vm, "Stack overflow");
                        vm->running = 0;
                    }
                    free(out);
                    free(sa);
                    free(sb);
                    hvm_free_value(&a);
                    hvm_free_value(&b);
                    break;
                }

                if (!hvm_is_numeric(a) || !hvm_is_numeric(b)) {
                    hvm_set_error_msg(vm, "ADD requires numeric or string operands");
                    vm->running = 0;
                    hvm_free_value(&a);
                    hvm_free_value(&b);
                    break;
                }

                if (a.type == HVM_TYPE_FLOAT || b.type == HVM_TYPE_FLOAT) {
                    if (!hvm_push_float(vm, hvm_to_double(a) + hvm_to_double(b))) {
                        hvm_set_error_msg(vm, "Stack overflow");
                        vm->running = 0;
                    }
                } else {
                    if (!hvm_push_int(vm, (int64_t)(hvm_to_double(a) + hvm_to_double(b)))) {
                        hvm_set_error_msg(vm, "Stack overflow");
                        vm->running = 0;
                    }
                }
                hvm_free_value(&a);
                hvm_free_value(&b);
                break;
            }

                        case HVM_SUB:
            case HVM_MUL:
            case HVM_DIV:
            case HVM_MOD: {
                HVM_Value a, b;
                double av, bv, rv;
                if (vm->stack_top < 2) {
                    hvm_set_error_msg(vm, "Stack underflow in arithmetic op");
                    vm->running = 0;
                    break;
                }
                b = hvm_pop(vm);
                a = hvm_pop(vm);

                if (!hvm_is_numeric(a) || !hvm_is_numeric(b)) {
                    hvm_set_error_msg(vm, "Arithmetic ops require numeric operands");
                    vm->running = 0;
                    hvm_free_value(&a);
                    hvm_free_value(&b);
                    break;
                }

                av = hvm_to_double(a);
                bv = hvm_to_double(b);

                if (instr->opcode == HVM_SUB) {
                    rv = av - bv;
                } else if (instr->opcode == HVM_MUL) {
                    rv = av * bv;
                } else if (instr->opcode == HVM_DIV) {
                    if (bv == 0.0) {
                        hvm_set_error_msg(vm, "Divide by zero");
                        vm->running = 0;
                        hvm_free_value(&a);
                        hvm_free_value(&b);
                        break;
                    }
                    rv = av / bv;
                } else {
                    int64_t ai;
                    int64_t bi;
                    if (bv == 0.0) {
                        hvm_set_error_msg(vm, "Modulo by zero");
                        vm->running = 0;
                        hvm_free_value(&a);
                        hvm_free_value(&b);
                        break;
                    }
                    ai = (int64_t)av;
                    bi = (int64_t)bv;
                    rv = (double)(ai % bi);
                }

                if (a.type == HVM_TYPE_FLOAT || b.type == HVM_TYPE_FLOAT || instr->opcode == HVM_DIV) {
                    if (!hvm_push_float(vm, rv)) {
                        hvm_set_error_msg(vm, "Stack overflow");
                        vm->running = 0;
                    }
                } else {
                    if (!hvm_push_int(vm, (int64_t)rv)) {
                        hvm_set_error_msg(vm, "Stack overflow");
                        vm->running = 0;
                    }
                }

                hvm_free_value(&a);
                hvm_free_value(&b);
                break;
            }

            case HVM_EQ:
            case HVM_NE:
            case HVM_LT:
            case HVM_LE:
            case HVM_GT:
            case HVM_GE: {
                HVM_Value a, b;
                int result = 0;
                if (vm->stack_top < 2) {
                    hvm_set_error_msg(vm, "Stack underflow in comparison");
                    vm->running = 0;
                    break;
                }
                b = hvm_pop(vm);
                a = hvm_pop(vm);

                if ((a.type == HVM_TYPE_STRING || b.type == HVM_TYPE_STRING) &&
                    (instr->opcode == HVM_LT || instr->opcode == HVM_LE || instr->opcode == HVM_GT || instr->opcode == HVM_GE)) {
                    hvm_set_error_msg(vm, "Ordering comparison does not support strings");
                    vm->running = 0;
                    hvm_free_value(&a);
                    hvm_free_value(&b);
                    break;
                }

                if (a.type == HVM_TYPE_STRING || b.type == HVM_TYPE_STRING) {
                    const char *sa = (a.type == HVM_TYPE_STRING && a.data.string_value) ? a.data.string_value : "";
                    const char *sb = (b.type == HVM_TYPE_STRING && b.data.string_value) ? b.data.string_value : "";
                    int cmp = strcmp(sa, sb);
                    result = (instr->opcode == HVM_EQ) ? (cmp == 0) : (cmp != 0);
                } else {
                    double av = hvm_to_double(a);
                    double bv = hvm_to_double(b);
                    switch (instr->opcode) {
                        case HVM_EQ: result = (av == bv); break;
                        case HVM_NE: result = (av != bv); break;
                        case HVM_LT: result = (av < bv); break;
                        case HVM_LE: result = (av <= bv); break;
                        case HVM_GT: result = (av > bv); break;
                        case HVM_GE: result = (av >= bv); break;
                        default: result = 0; break;
                    }
                }

                if (!hvm_push_bool(vm, result)) {
                    hvm_set_error_msg(vm, "Stack overflow");
                    vm->running = 0;
                }

                hvm_free_value(&a);
                hvm_free_value(&b);
                break;
            }

            case HVM_AND:
            case HVM_OR: {
                HVM_Value a, b;
                int result;
                if (vm->stack_top < 2) {
                    hvm_set_error_msg(vm, "Stack underflow in logical op");
                    vm->running = 0;
                    break;
                }
                b = hvm_pop(vm);
                a = hvm_pop(vm);
                result = (instr->opcode == HVM_AND) ? (hvm_is_truthy(a) && hvm_is_truthy(b)) : (hvm_is_truthy(a) || hvm_is_truthy(b));
                if (!hvm_push_bool(vm, result)) {
                    hvm_set_error_msg(vm, "Stack overflow");
                    vm->running = 0;
                }
                hvm_free_value(&a);
                hvm_free_value(&b);
                break;
            }

            case HVM_NOT: {
                HVM_Value v;
                if (vm->stack_top < 1) {
                    hvm_set_error_msg(vm, "Stack underflow in NOT");
                    vm->running = 0;
                    break;
                }
                v = hvm_pop(vm);
                if (!hvm_push_bool(vm, !hvm_is_truthy(v))) {
                    hvm_set_error_msg(vm, "Stack overflow");
                    vm->running = 0;
                }
                hvm_free_value(&v);
                break;
            }

            case HVM_STORE_GLOBAL: {
                HVM_Value v;
                if (vm->stack_top < 1) {
                    hvm_set_error_msg(vm, "Stack underflow in STORE_GLOBAL");
                    vm->running = 0;
                    break;
                }
                v = hvm_pop(vm);
                if (!store_global(vm, instr->operand.string_operand, v)) {
                    hvm_free_value(&v);
                    hvm_set_error_msg(vm, "Failed to store global variable");
                    vm->running = 0;
                }
                break;
            }

            case HVM_LOAD_GLOBAL: {
                HVM_Value v;
                memset(&v, 0, sizeof(v));
                v.type = HVM_TYPE_INT;
                if (!load_global(vm, instr->operand.string_operand, &v)) {
                    if (!hvm_push_int(vm, 0)) {
                        hvm_set_error_msg(vm, "Stack overflow");
                        vm->running = 0;
                    }
                } else if (v.type == HVM_TYPE_FLOAT) {
                    if (!hvm_push_float(vm, v.data.float_value)) {
                        hvm_set_error_msg(vm, "Stack overflow");
                        vm->running = 0;
                    }
                } else if (v.type == HVM_TYPE_STRING) {
                    if (!hvm_push_string(vm, v.data.string_value ? v.data.string_value : "")) {
                        hvm_set_error_msg(vm, "Stack overflow");
                        vm->running = 0;
                    }
                } else if (v.type == HVM_TYPE_BOOL) {
                    if (!hvm_push_bool(vm, v.data.bool_value)) {
                        hvm_set_error_msg(vm, "Stack overflow");
                        vm->running = 0;
                    }
                } else {
                    if (!hvm_push_int(vm, v.data.int_value)) {
                        hvm_set_error_msg(vm, "Stack overflow");
                        vm->running = 0;
                    }
                }
                break;
            }

            case HVM_PRINT:
            case HVM_PRINTLN: {
                HVM_Value value;
                if (vm->stack_top < 1) {
                    hvm_set_error_msg(vm, "Stack underflow in PRINT");
                    vm->running = 0;
                    break;
                }
                value = hvm_pop(vm);
                switch (value.type) {
                    case HVM_TYPE_INT:
                        printf("%lld", (long long)value.data.int_value);
                        break;
                    case HVM_TYPE_FLOAT:
                        printf("%g", value.data.float_value);
                        break;
                    case HVM_TYPE_STRING:
                        printf("%s", value.data.string_value ? value.data.string_value : "");
                        break;
                    case HVM_TYPE_BOOL:
                        printf("%s", value.data.bool_value ? "true" : "false");
                        break;
                    default:
                        printf("null");
                        break;
                }
                if (instr->opcode == HVM_PRINTLN) printf("\n");
                hvm_free_value(&value);
                break;
            }

            case HVM_CREATE_WINDOW: {
                int success = gui_create_window(instr->operand.string_operand ? instr->operand.string_operand : "HOSC VM Window");
                if (success) {
                    GUI_LOG("[GUI] real window: %s\n", instr->operand.string_operand ? instr->operand.string_operand : "HOSC VM Window");
                } else {
                    g_gui.using_real = 0;
                    g_gui.running = 1;
                    gui_free_commands();
                    gui_free_inputs();
                    gui_free_textareas();
                    gui_free_widgets();
                    gui_reset_style_defaults();
                    g_gui.last_tick = (uint64_t)gui_now_ms();
                    g_gui.frame_index = 1;
                    GUI_LOG("[GUI] window (console fallback): %s\n", instr->operand.string_operand ? instr->operand.string_operand : "HOSC VM Window");
                }
                hvm_push_bool(vm, success);
                break;
            }

            case HVM_CLEAR: {
                HVM_Value b, g, r;
                int ri, gi, bi;
                if (vm->stack_top < 3) { hvm_set_error_msg(vm, "Stack underflow in CLEAR"); vm->running = 0; break; }
                b = hvm_pop(vm); g = hvm_pop(vm); r = hvm_pop(vm);
                ri = (r.type == HVM_TYPE_FLOAT) ? (int)r.data.float_value : (int)r.data.int_value;
                gi = (g.type == HVM_TYPE_FLOAT) ? (int)g.data.float_value : (int)g.data.int_value;
                bi = (b.type == HVM_TYPE_FLOAT) ? (int)b.data.float_value : (int)b.data.int_value;
                g_gui.bg_color = RGB((ri)&0xFF, (gi)&0xFF, (bi)&0xFF);
                gui_clear_commands();
                if (g_gui.using_real && g_gui.running) { /* repaint in HVM_LOOP */ }
                hvm_push_int(vm, 0);
                hvm_free_value(&b); hvm_free_value(&g); hvm_free_value(&r);
                break;
            }

            case HVM_SET_BG_COLOR: {
                HVM_Value b, g, r;
                int ri, gi, bi;
                if (vm->stack_top < 3) { hvm_set_error_msg(vm, "Stack underflow in SET_BG_COLOR"); vm->running = 0; break; }
                b = hvm_pop(vm); g = hvm_pop(vm); r = hvm_pop(vm);
                ri = (r.type == HVM_TYPE_FLOAT) ? (int)r.data.float_value : (int)r.data.int_value;
                gi = (g.type == HVM_TYPE_FLOAT) ? (int)g.data.float_value : (int)g.data.int_value;
                bi = (b.type == HVM_TYPE_FLOAT) ? (int)b.data.float_value : (int)b.data.int_value;
                g_gui.bg_color = RGB((ri)&0xFF, (gi)&0xFF, (bi)&0xFF);
                if (g_gui.using_real && g_gui.running) { /* repaint in HVM_LOOP */ }
                hvm_push_int(vm, 0);
                hvm_free_value(&b); hvm_free_value(&g); hvm_free_value(&r);
                break;
            }

            case HVM_SET_COLOR: {
                HVM_Value b, g, r;
                int ri, gi, bi;
                if (vm->stack_top < 3) { hvm_set_error_msg(vm, "Stack underflow in SET_COLOR"); vm->running = 0; break; }
                b = hvm_pop(vm); g = hvm_pop(vm); r = hvm_pop(vm);
                ri = (r.type == HVM_TYPE_FLOAT) ? (int)r.data.float_value : (int)r.data.int_value;
                gi = (g.type == HVM_TYPE_FLOAT) ? (int)g.data.float_value : (int)g.data.int_value;
                bi = (b.type == HVM_TYPE_FLOAT) ? (int)b.data.float_value : (int)b.data.int_value;
                g_gui.fg_color = RGB((ri)&0xFF, (gi)&0xFF, (bi)&0xFF);
                hvm_push_int(vm, 0);
                hvm_free_value(&b); hvm_free_value(&g); hvm_free_value(&r);
                break;
            }

            case HVM_SET_FONT_SIZE: {
                HVM_Value v;
                int size;
                if (vm->stack_top < 1) { hvm_set_error_msg(vm, "Stack underflow in SET_FONT_SIZE"); vm->running = 0; break; }
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
                break;
            }

            case HVM_DRAW_TEXT: {
                HVM_Value msg, y, x;
                const char *text;
                long long xi, yi;
                HVM_GuiCmd cmd;
                if (vm->stack_top < 3) { hvm_set_error_msg(vm, "Stack underflow in DRAW_TEXT"); vm->running = 0; break; }
                msg = hvm_pop(vm); y = hvm_pop(vm); x = hvm_pop(vm);
                text = (msg.type == HVM_TYPE_STRING && msg.data.string_value) ? msg.data.string_value : "";
                xi = (x.type == HVM_TYPE_FLOAT) ? (long long)x.data.float_value : (long long)x.data.int_value;
                yi = (y.type == HVM_TYPE_FLOAT) ? (long long)y.data.float_value : (long long)y.data.int_value;
                memset(&cmd, 0, sizeof(cmd));
                cmd.type = GUI_CMD_TEXT;
                cmd.x = (int)xi; cmd.y = (int)yi; cmd.color = g_gui.fg_color; cmd.text = (char *)text;
                if (g_gui.using_real && g_gui.running) {
                    if (!gui_push_cmd(&cmd)) { hvm_set_error_msg(vm, "Out of memory while queueing GUI text"); vm->running = 0; }
                    else { /* repaint in HVM_LOOP */ }
                } else if (!g_gui.using_real) {
                    GUI_LOG("[GUI] text (%lld,%lld): %s\n", xi, yi, text);
                }
                hvm_push_int(vm, 0);
                hvm_free_value(&msg); hvm_free_value(&y); hvm_free_value(&x);
                break;
            }

            case HVM_DRAW_BUTTON: {
                HVM_Value label, h, w, y, x;
                long long xi, yi, wi, hi;
                int clicked = 0;
                HVM_GuiCmd cmd;
                HVM_WidgetState *ws;
                size_t widx = (size_t)(-1);
                const char *button_id;
                if (vm->stack_top < 5) { hvm_set_error_msg(vm, "Stack underflow in DRAW_BUTTON"); vm->running = 0; break; }
                label = hvm_pop(vm); h = hvm_pop(vm); w = hvm_pop(vm); y = hvm_pop(vm); x = hvm_pop(vm);
                xi = (x.type == HVM_TYPE_FLOAT) ? (long long)x.data.float_value : (long long)x.data.int_value;
                yi = (y.type == HVM_TYPE_FLOAT) ? (long long)y.data.float_value : (long long)y.data.int_value;
                wi = (w.type == HVM_TYPE_FLOAT) ? (long long)w.data.float_value : (long long)w.data.int_value;
                hi = (h.type == HVM_TYPE_FLOAT) ? (long long)h.data.float_value : (long long)h.data.int_value;
                button_id = (label.type == HVM_TYPE_STRING && label.data.string_value) ? label.data.string_value : "";
                ws = gui_register_widget_rect(button_id, (int)xi, (int)yi, (int)wi, (int)hi);
                if (ws) widx = (size_t)(ws - g_gui.widgets);
                clicked = gui_take_mouse_click_in_rect((int)xi, (int)yi, (int)wi, (int)hi);
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
                    if (!gui_push_cmd(&cmd)) { hvm_set_error_msg(vm, "Out of memory while queueing button"); vm->running = 0; }
                } else if (!g_gui.using_real) {
                    GUI_LOG("[GUI] button (%lld,%lld,%lld,%lld): %s\n", xi, yi, wi, hi, cmd.text ? cmd.text : "");
                }
                hvm_push_bool(vm, clicked);
                hvm_free_value(&label); hvm_free_value(&h); hvm_free_value(&w); hvm_free_value(&y); hvm_free_value(&x);
                break;
            }

            case HVM_DRAW_BUTTON_STATE: {
                HVM_Value label_value, id_value;
                char *id_name = NULL;
                char *label_text = NULL;
                int bx, by, bw, bh;
                int clicked;
                HVM_GuiCmd cmd;
                HVM_WidgetState *ws;
                size_t widx = (size_t)(-1);

                if (vm->stack_top < 2) { hvm_set_error_msg(vm, "Stack underflow in DRAW_BUTTON_STATE"); vm->running = 0; break; }
                label_value = hvm_pop(vm);
                id_value = hvm_pop(vm);

                if (id_value.type == HVM_TYPE_STRING) id_name = strdup(id_value.data.string_value ? id_value.data.string_value : "");
                else id_name = hvm_value_to_string(id_value);

                if (label_value.type == HVM_TYPE_STRING) label_text = strdup(label_value.data.string_value ? label_value.data.string_value : "Button");
                else label_text = hvm_value_to_string(label_value);

                if (!id_name || !label_text) {
                    free(id_name);
                    free(label_text);
                    hvm_free_value(&label_value);
                    hvm_free_value(&id_value);
                    hvm_set_error_msg(vm, "Out of memory in DRAW_BUTTON_STATE");
                    vm->running = 0;
                    break;
                }

                gui_layout_place_widget(180, 34, &bx, &by, &bw, &bh);
                ws = gui_register_widget_rect(id_name, bx, by, bw, bh);
                if (ws) widx = (size_t)(ws - g_gui.widgets);

                clicked = gui_take_mouse_click_in_rect(bx, by, bw, bh);
                if (ws) ws->clicked = clicked;

                memset(&cmd, 0, sizeof(cmd));
                cmd.type = GUI_CMD_BUTTON;
                cmd.x = bx;
                cmd.y = by;
                cmd.w = bw;
                cmd.h = bh;
                cmd.text = label_text;
                cmd.payload = (ws && widx != (size_t)(-1)) ? GUI_PAYLOAD_INDEX(widx) : NULL;

                if (g_gui.using_real && g_gui.running) {
                    if (!gui_push_cmd(&cmd)) { hvm_set_error_msg(vm, "Out of memory while queueing state button"); vm->running = 0; }
                } else if (!g_gui.using_real) {
                    GUI_LOG("[GUI] button(id=%s) (%d,%d,%d,%d): %s\n", id_name, bx, by, bw, bh, label_text);
                }

                hvm_push_bool(vm, clicked);

                free(id_name);
                free(label_text);
                hvm_free_value(&label_value);
                hvm_free_value(&id_value);
                break;
            }
            case HVM_DRAW_INPUT: {
                HVM_Value label, w, y, x;
                long long xi, yi, wi;
                HVM_GuiCmd cmd;
                HVM_InputState *st;
                size_t idx;
                if (vm->stack_top < 4) { hvm_set_error_msg(vm, "Stack underflow in DRAW_INPUT"); vm->running = 0; break; }
                label = hvm_pop(vm); w = hvm_pop(vm); y = hvm_pop(vm); x = hvm_pop(vm);
                xi = (x.type == HVM_TYPE_FLOAT) ? (long long)x.data.float_value : (long long)x.data.int_value;
                yi = (y.type == HVM_TYPE_FLOAT) ? (long long)y.data.float_value : (long long)y.data.int_value;
                wi = (w.type == HVM_TYPE_FLOAT) ? (long long)w.data.float_value : (long long)w.data.int_value;
                st = gui_get_input_state((label.type == HVM_TYPE_STRING && label.data.string_value) ? label.data.string_value : "");
                idx = st ? (size_t)(st - g_gui.inputs) : GUI_NO_ACTIVE_INPUT;
                memset(&cmd, 0, sizeof(cmd));
                cmd.type = GUI_CMD_INPUT;
                cmd.x = (int)xi;
                cmd.y = (int)yi;
                cmd.w = (int)wi;
                cmd.h = 28;
                cmd.text = (label.type == HVM_TYPE_STRING && label.data.string_value) ? label.data.string_value : "";
                cmd.payload = (idx != GUI_NO_ACTIVE_INPUT) ? GUI_PAYLOAD_INDEX(idx) : NULL;
                if (gui_take_mouse_focus_in_rect(cmd.x, cmd.y, cmd.w, cmd.h)) {
                    g_gui.active_input = idx;
                    g_gui.active_textarea = GUI_NO_ACTIVE_TEXTAREA;
                }
                if (g_gui.using_real && g_gui.running) {
                    if (!gui_push_cmd(&cmd)) { hvm_set_error_msg(vm, "Out of memory while queueing input"); vm->running = 0; }
                } else if (!g_gui.using_real) {
                    GUI_LOG("[GUI] input (%lld,%lld,%lld): %s\n", xi, yi, wi, cmd.text ? cmd.text : "");
                }
                if (st && st->buffer) hvm_push_string(vm, st->buffer); else hvm_push_string(vm, "");
                hvm_free_value(&label); hvm_free_value(&w); hvm_free_value(&y); hvm_free_value(&x);
                break;
            }

            case HVM_DRAW_INPUT_STATE: {
                HVM_Value placeholder_value, id_value;
                char *id_name = NULL;
                char *placeholder = NULL;
                HVM_GuiCmd cmd;
                HVM_InputState *st;
                size_t idx;
                int ix, iy, iw, ih;

                if (vm->stack_top < 2) { hvm_set_error_msg(vm, "Stack underflow in DRAW_INPUT_STATE"); vm->running = 0; break; }
                placeholder_value = hvm_pop(vm);
                id_value = hvm_pop(vm);

                if (id_value.type == HVM_TYPE_STRING) id_name = strdup(id_value.data.string_value ? id_value.data.string_value : "");
                else id_name = hvm_value_to_string(id_value);

                if (placeholder_value.type == HVM_TYPE_STRING) placeholder = strdup(placeholder_value.data.string_value ? placeholder_value.data.string_value : "");
                else placeholder = hvm_value_to_string(placeholder_value);

                if (!id_name || !placeholder) {
                    free(id_name);
                    free(placeholder);
                    hvm_free_value(&placeholder_value);
                    hvm_free_value(&id_value);
                    hvm_set_error_msg(vm, "Out of memory in DRAW_INPUT_STATE");
                    vm->running = 0;
                    break;
                }

                st = gui_get_input_state(id_name);
                idx = st ? (size_t)(st - g_gui.inputs) : GUI_NO_ACTIVE_INPUT;
                if (st && st->len == 0 && placeholder[0] != '\0') {
                    gui_buffer_set(&st->buffer, &st->len, &st->cap, placeholder);
                }

                gui_layout_place_widget(260, 30, &ix, &iy, &iw, &ih);
                gui_register_widget_rect(id_name, ix, iy, iw, ih);

                memset(&cmd, 0, sizeof(cmd));
                cmd.type = GUI_CMD_INPUT;
                cmd.x = ix;
                cmd.y = iy;
                cmd.w = iw;
                cmd.h = ih;
                cmd.text = placeholder;
                cmd.payload = (idx != GUI_NO_ACTIVE_INPUT) ? GUI_PAYLOAD_INDEX(idx) : NULL;

                if (gui_take_mouse_focus_in_rect(cmd.x, cmd.y, cmd.w, cmd.h)) {
                    g_gui.active_input = idx;
                    g_gui.active_textarea = GUI_NO_ACTIVE_TEXTAREA;
                }

                if (g_gui.using_real && g_gui.running) {
                    if (!gui_push_cmd(&cmd)) { hvm_set_error_msg(vm, "Out of memory while queueing state input"); vm->running = 0; }
                } else if (!g_gui.using_real) {
                    GUI_LOG("[GUI] input(id=%s) (%d,%d,%d): %s\n", id_name, ix, iy, iw, placeholder);
                }

                if (st && st->buffer) hvm_push_string(vm, st->buffer); else hvm_push_string(vm, "");

                free(id_name);
                free(placeholder);
                hvm_free_value(&placeholder_value);
                hvm_free_value(&id_value);
                break;
            }
            case HVM_DRAW_TEXTAREA: {
                HVM_Value id, h, w, y, x;
                long long xi, yi, wi, hi;
                HVM_GuiCmd cmd;
                HVM_TextAreaState *ta;
                size_t idx;
                if (vm->stack_top < 5) { hvm_set_error_msg(vm, "Stack underflow in DRAW_TEXTAREA"); vm->running = 0; break; }
                id = hvm_pop(vm); h = hvm_pop(vm); w = hvm_pop(vm); y = hvm_pop(vm); x = hvm_pop(vm);
                xi = (x.type == HVM_TYPE_FLOAT) ? (long long)x.data.float_value : (long long)x.data.int_value;
                yi = (y.type == HVM_TYPE_FLOAT) ? (long long)y.data.float_value : (long long)y.data.int_value;
                wi = (w.type == HVM_TYPE_FLOAT) ? (long long)w.data.float_value : (long long)w.data.int_value;
                hi = (h.type == HVM_TYPE_FLOAT) ? (long long)h.data.float_value : (long long)h.data.int_value;
                if (hi < 40) hi = 40;
                ta = gui_get_textarea_state((id.type == HVM_TYPE_STRING && id.data.string_value) ? id.data.string_value : "");
                idx = ta ? (size_t)(ta - g_gui.textareas) : GUI_NO_ACTIVE_TEXTAREA;
                memset(&cmd, 0, sizeof(cmd));
                cmd.type = GUI_CMD_TEXTAREA;
                cmd.x = (int)xi;
                cmd.y = (int)yi;
                cmd.w = (int)wi;
                cmd.h = (int)hi;
                cmd.text = (id.type == HVM_TYPE_STRING && id.data.string_value) ? id.data.string_value : "";
                cmd.payload = (idx != GUI_NO_ACTIVE_TEXTAREA) ? GUI_PAYLOAD_INDEX(idx) : NULL;
                if (gui_take_mouse_focus_in_rect(cmd.x, cmd.y, cmd.w, cmd.h)) {
                    g_gui.active_textarea = idx;
                    g_gui.active_input = GUI_NO_ACTIVE_INPUT;
                }
                if (g_gui.using_real && g_gui.running) {
                    if (!gui_push_cmd(&cmd)) { hvm_set_error_msg(vm, "Out of memory while queueing textarea"); vm->running = 0; }
                } else if (!g_gui.using_real) {
                    GUI_LOG("[GUI] textarea (%lld,%lld,%lld,%lld): %s\n", xi, yi, wi, hi, cmd.text ? cmd.text : "");
                }
                if (ta && ta->buffer) hvm_push_string(vm, ta->buffer); else hvm_push_string(vm, "");
                hvm_free_value(&id); hvm_free_value(&h); hvm_free_value(&w); hvm_free_value(&y); hvm_free_value(&x);
                break;
            }

            case HVM_DRAW_IMAGE: {
                HVM_Value path, h, w, y, x;
                long long xi, yi, wi, hi;
                HVM_GuiCmd cmd;
                if (vm->stack_top < 5) { hvm_set_error_msg(vm, "Stack underflow in DRAW_IMAGE"); vm->running = 0; break; }
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
                    if (!gui_push_cmd(&cmd)) { hvm_set_error_msg(vm, "Out of memory while queueing image"); vm->running = 0; }
                    else { /* repaint in HVM_LOOP */ }
                } else if (!g_gui.using_real) {
                    GUI_LOG("[GUI] image (%lld,%lld,%lld,%lld): %s\n", xi, yi, wi, hi, cmd.text);
                }
                hvm_push_int(vm, 0);
                hvm_free_value(&path); hvm_free_value(&h); hvm_free_value(&w); hvm_free_value(&y); hvm_free_value(&x);
                break;
            }

            case HVM_GET_MOUSE_X:
                hvm_push_int(vm, g_gui.mouse_x);
                break;

            case HVM_GET_MOUSE_Y:
                hvm_push_int(vm, g_gui.mouse_y);
                break;

            case HVM_IS_MOUSE_DOWN:
                hvm_push_bool(vm, g_gui.mouse_down);
                break;

            case HVM_WAS_MOUSE_UP:
                hvm_push_bool(vm, g_gui.mouse_up);
                g_gui.mouse_up = 0;
                break;

            case HVM_WAS_MOUSE_CLICK:
                hvm_push_bool(vm, g_gui.mouse_clicked);
                g_gui.mouse_clicked = 0;
                g_gui.mouse_click_consumed = 0;
                break;

            case HVM_IS_MOUSE_HOVER: {
                HVM_Value id;
                char *id_name;
                HVM_WidgetState *ws;
                int hovered = 0;
                if (vm->stack_top < 1) { hvm_set_error_msg(vm, "Stack underflow in IS_MOUSE_HOVER"); vm->running = 0; break; }
                id = hvm_pop(vm);
                if (id.type == HVM_TYPE_STRING) id_name = strdup(id.data.string_value ? id.data.string_value : "");
                else id_name = hvm_value_to_string(id);
                if (!id_name) {
                    hvm_free_value(&id);
                    hvm_set_error_msg(vm, "Out of memory in IS_MOUSE_HOVER");
                    vm->running = 0;
                    break;
                }
                ws = gui_find_widget_state(id_name);
                if (ws && ws->frame_seen == g_gui.frame_index) {
                    hovered = gui_point_in_rect(g_gui.mouse_x, g_gui.mouse_y, ws->x, ws->y, ws->w, ws->h);
                }
                hvm_push_bool(vm, hovered);
                free(id_name);
                hvm_free_value(&id);
                break;
            }

            case HVM_IS_KEY_DOWN: {
                HVM_Value key;
                int code;
                if (vm->stack_top < 1) { hvm_set_error_msg(vm, "Stack underflow in IS_KEY_DOWN"); vm->running = 0; break; }
                key = hvm_pop(vm);
                code = (key.type == HVM_TYPE_FLOAT) ? (int)key.data.float_value : (int)key.data.int_value;
                if (code < 0 || code >= 256) code = 0;
                hvm_push_bool(vm, g_gui.key_down[code & 0xFF]);
                hvm_free_value(&key);
                break;
            }

            case HVM_WAS_KEY_PRESS: {
                HVM_Value key;
                int code;
                int pressed;
                if (vm->stack_top < 1) { hvm_set_error_msg(vm, "Stack underflow in WAS_KEY_PRESS"); vm->running = 0; break; }
                key = hvm_pop(vm);
                code = (key.type == HVM_TYPE_FLOAT) ? (int)key.data.float_value : (int)key.data.int_value;
                if (code < 0 || code >= 256) code = 0;
                pressed = g_gui.key_pressed[code & 0xFF];
                g_gui.key_pressed[code & 0xFF] = 0;
                hvm_push_bool(vm, pressed);
                hvm_free_value(&key);
                break;
            }

            case HVM_DELTA_TIME:
                hvm_push_float(vm, (double)g_gui.delta_ms);
                break;

            case HVM_LAYOUT_RESET: {
                HVM_Value gap, y, x;
                if (vm->stack_top < 3) { hvm_set_error_msg(vm, "Stack underflow in LAYOUT_RESET"); vm->running = 0; break; }
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
                break;
            }

            case HVM_LAYOUT_NEXT: {
                HVM_Value h;
                int height;
                int y;
                if (vm->stack_top < 1) { hvm_set_error_msg(vm, "Stack underflow in LAYOUT_NEXT"); vm->running = 0; break; }
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
                break;
            }

            case HVM_LAYOUT_ROW: {
                HVM_Value cols_value;
                int cols;
                if (vm->stack_top < 1) { hvm_set_error_msg(vm, "Stack underflow in LAYOUT_ROW"); vm->running = 0; break; }
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
                break;
            }

            case HVM_LAYOUT_COLUMN: {
                HVM_Value width_value;
                int width;
                if (vm->stack_top < 1) { hvm_set_error_msg(vm, "Stack underflow in LAYOUT_COLUMN"); vm->running = 0; break; }
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
                break;
            }

            case HVM_LAYOUT_GRID: {
                HVM_Value cell_h_value, cell_w_value, cols_value;
                int cols;
                int cell_w;
                int cell_h;
                if (vm->stack_top < 3) { hvm_set_error_msg(vm, "Stack underflow in LAYOUT_GRID"); vm->running = 0; break; }
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
                break;
            }

            case HVM_LOOP: {
                double now = gui_now_ms();
                g_gui.delta_ms = (g_gui.last_tick == 0) ? 0.0 : (now - (double)g_gui.last_tick);
                g_gui.last_tick = (uint64_t)now;
                g_gui.loop_called = 1;

                gui_reset_transient_input();

                if (g_gui.using_real && !g_gui.running) {
                    vm->running = 0;
                } else if (g_gui.using_real && g_gui.running) {
                    gui_pump_events();
                    gui_apply_click_focus_from_commands();
                    gui_request_repaint();
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
                break;
            }

            case HVM_MENU_SETUP_NOTEPAD: {
                int ok = 0;
                if (g_gui.using_real && g_gui.running) {
                    ok = gui_menu_setup_notepad();
                }
                hvm_push_bool(vm, ok);
                break;
            }

            case HVM_MENU_EVENT:
                if (g_gui.using_real && g_gui.running) gui_pump_events();
                hvm_push_int(vm, gui_take_menu_event());
                break;

            case HVM_SCROLL_SET_RANGE: {
                HVM_Value total;
                int total_height;
                if (vm->stack_top < 1) { hvm_set_error_msg(vm, "Stack underflow in SCROLL_SET_RANGE"); vm->running = 0; break; }
                total = hvm_pop(vm);
                total_height = (total.type == HVM_TYPE_FLOAT) ? (int)total.data.float_value : (int)total.data.int_value;
                if (total_height < 0) total_height = 0;
                g_gui.scroll_range = total_height;
#ifdef _WIN32
                if (g_gui.using_real && g_gui.running) gui_update_scrollbar();
#endif
                hvm_push_int(vm, g_gui.scroll_range);
                hvm_free_value(&total);
                break;
            }

            case HVM_SCROLL_Y:
                hvm_push_int(vm, g_gui.scroll_y);
                break;

            case HVM_FILE_OPEN_DIALOG: {
                char *path_value = NULL;
                if (g_gui.using_real && g_gui.running) path_value = gui_open_file_dialog();
                if (!path_value) path_value = strdup("");
                if (!path_value || !hvm_push_string(vm, path_value)) {
                    free(path_value);
                    hvm_set_error_msg(vm, "Failed to push OPEN dialog path");
                    vm->running = 0;
                    break;
                }
                free(path_value);
                break;
            }

            case HVM_FILE_SAVE_DIALOG: {
                char *path_value = NULL;
                if (g_gui.using_real && g_gui.running) path_value = gui_save_file_dialog();
                if (!path_value) path_value = strdup("");
                if (!path_value || !hvm_push_string(vm, path_value)) {
                    free(path_value);
                    hvm_set_error_msg(vm, "Failed to push SAVE dialog path");
                    vm->running = 0;
                    break;
                }
                free(path_value);
                break;
            }

            case HVM_FILE_READ: {
                HVM_Value path_value;
                char *path = NULL;
                char *content = NULL;
                if (vm->stack_top < 1) { hvm_set_error_msg(vm, "Stack underflow in FILE_READ"); vm->running = 0; break; }
                path_value = hvm_pop(vm);
                if (path_value.type == HVM_TYPE_STRING) path = strdup(path_value.data.string_value ? path_value.data.string_value : "");
                else path = hvm_value_to_string(path_value);
                if (!path) {
                    hvm_free_value(&path_value);
                    hvm_set_error_msg(vm, "Out of memory in FILE_READ path");
                    vm->running = 0;
                    break;
                }

                content = hvm_read_text_file(path);
                if (!content) {
                    free(path);
                    hvm_free_value(&path_value);
                    hvm_set_error_msg(vm, "Out of memory in FILE_READ content");
                    vm->running = 0;
                    break;
                }

                if (!hvm_push_string(vm, content)) {
                    hvm_set_error_msg(vm, "Stack overflow in FILE_READ");
                    vm->running = 0;
                }

                free(content);
                free(path);
                hvm_free_value(&path_value);
                break;
            }

            case HVM_FILE_READ_LINE: {
                HVM_Value line_value;
                HVM_Value path_value;
                char *path = NULL;
                char *line = NULL;
                int line_no;
                if (vm->stack_top < 2) { hvm_set_error_msg(vm, "Stack underflow in FILE_READ_LINE"); vm->running = 0; break; }
                line_value = hvm_pop(vm);
                path_value = hvm_pop(vm);

                line_no = (line_value.type == HVM_TYPE_FLOAT) ? (int)line_value.data.float_value : (int)line_value.data.int_value;
                if (path_value.type == HVM_TYPE_STRING) path = strdup(path_value.data.string_value ? path_value.data.string_value : "");
                else path = hvm_value_to_string(path_value);

                if (!path) {
                    hvm_free_value(&line_value);
                    hvm_free_value(&path_value);
                    hvm_set_error_msg(vm, "Out of memory in FILE_READ_LINE path");
                    vm->running = 0;
                    break;
                }

                line = hvm_read_text_line(path, line_no);
                if (!line) {
                    free(path);
                    hvm_free_value(&line_value);
                    hvm_free_value(&path_value);
                    hvm_set_error_msg(vm, "Out of memory in FILE_READ_LINE");
                    vm->running = 0;
                    break;
                }

                if (!hvm_push_string(vm, line)) {
                    hvm_set_error_msg(vm, "Stack overflow in FILE_READ_LINE");
                    vm->running = 0;
                }

                free(line);
                free(path);
                hvm_free_value(&line_value);
                hvm_free_value(&path_value);
                break;
            }

            case HVM_FILE_WRITE: {
                HVM_Value content_value;
                HVM_Value path_value;
                char *path = NULL;
                char *text = NULL;
                int ok;
                if (vm->stack_top < 2) { hvm_set_error_msg(vm, "Stack underflow in FILE_WRITE"); vm->running = 0; break; }
                content_value = hvm_pop(vm);
                path_value = hvm_pop(vm);

                if (path_value.type == HVM_TYPE_STRING) path = strdup(path_value.data.string_value ? path_value.data.string_value : "");
                else path = hvm_value_to_string(path_value);

                if (content_value.type == HVM_TYPE_STRING) text = strdup(content_value.data.string_value ? content_value.data.string_value : "");
                else text = hvm_value_to_string(content_value);

                if (!path || !text) {
                    free(path);
                    free(text);
                    hvm_free_value(&content_value);
                    hvm_free_value(&path_value);
                    hvm_set_error_msg(vm, "Out of memory in FILE_WRITE");
                    vm->running = 0;
                    break;
                }

                ok = hvm_write_text_file(path, text);
                if (!hvm_push_bool(vm, ok)) {
                    hvm_set_error_msg(vm, "Stack overflow in FILE_WRITE");
                    vm->running = 0;
                }

                free(path);
                free(text);
                hvm_free_value(&content_value);
                hvm_free_value(&path_value);
                break;
            }

            case HVM_INPUT_SET: {
                HVM_Value value;
                HVM_Value label;
                char *label_name = NULL;
                char *text_value = NULL;
                HVM_InputState *st;
                int ok = 0;

                if (vm->stack_top < 2) { hvm_set_error_msg(vm, "Stack underflow in INPUT_SET"); vm->running = 0; break; }

                value = hvm_pop(vm);
                label = hvm_pop(vm);

                if (label.type == HVM_TYPE_STRING) label_name = strdup(label.data.string_value ? label.data.string_value : "");
                else label_name = hvm_value_to_string(label);

                if (value.type == HVM_TYPE_STRING) text_value = strdup(value.data.string_value ? value.data.string_value : "");
                else text_value = hvm_value_to_string(value);

                if (!label_name || !text_value) {
                    free(label_name);
                    free(text_value);
                    hvm_free_value(&value);
                    hvm_free_value(&label);
                    hvm_set_error_msg(vm, "Out of memory in INPUT_SET");
                    vm->running = 0;
                    break;
                }

                st = gui_get_input_state(label_name);
                if (st) {
                    ok = gui_buffer_set(&st->buffer, &st->len, &st->cap, text_value);
                    if (!ok) {
                        hvm_set_error_msg(vm, "Out of memory resizing INPUT_SET");
                        vm->running = 0;
                    }
                }

                if (!hvm_push_bool(vm, ok)) {
                    hvm_set_error_msg(vm, "Stack overflow in INPUT_SET");
                    vm->running = 0;
                }

                free(label_name);
                free(text_value);
                hvm_free_value(&value);
                hvm_free_value(&label);
                break;
            }

            case HVM_TEXTAREA_SET: {
                HVM_Value value;
                HVM_Value id;
                char *id_name = NULL;
                char *text_value = NULL;
                HVM_TextAreaState *ta;
                int ok = 0;

                if (vm->stack_top < 2) { hvm_set_error_msg(vm, "Stack underflow in TEXTAREA_SET"); vm->running = 0; break; }

                value = hvm_pop(vm);
                id = hvm_pop(vm);

                if (id.type == HVM_TYPE_STRING) id_name = strdup(id.data.string_value ? id.data.string_value : "");
                else id_name = hvm_value_to_string(id);

                if (value.type == HVM_TYPE_STRING) text_value = strdup(value.data.string_value ? value.data.string_value : "");
                else text_value = hvm_value_to_string(value);

                if (!id_name || !text_value) {
                    free(id_name);
                    free(text_value);
                    hvm_free_value(&value);
                    hvm_free_value(&id);
                    hvm_set_error_msg(vm, "Out of memory in TEXTAREA_SET");
                    vm->running = 0;
                    break;
                }

                ta = gui_get_textarea_state(id_name);
                if (ta) {
                    ok = gui_buffer_set(&ta->buffer, &ta->len, &ta->cap, text_value);
                    if (!ok) {
                        hvm_set_error_msg(vm, "Out of memory resizing TEXTAREA_SET");
                        vm->running = 0;
                    }
                }

                if (!hvm_push_bool(vm, ok)) {
                    hvm_set_error_msg(vm, "Stack overflow in TEXTAREA_SET");
                    vm->running = 0;
                }

                free(id_name);
                free(text_value);
                hvm_free_value(&value);
                hvm_free_value(&id);
                break;
            }

            case HVM_EXEC_COMMAND: {
                HVM_Value cmd_value;
                char *cmd = NULL;
                char *out = NULL;

                if (vm->stack_top < 1) { hvm_set_error_msg(vm, "Stack underflow in EXEC_COMMAND"); vm->running = 0; break; }

                cmd_value = hvm_pop(vm);
                if (cmd_value.type == HVM_TYPE_STRING) cmd = strdup(cmd_value.data.string_value ? cmd_value.data.string_value : "");
                else cmd = hvm_value_to_string(cmd_value);

                if (!cmd) {
                    hvm_free_value(&cmd_value);
                    hvm_set_error_msg(vm, "Out of memory in EXEC_COMMAND");
                    vm->running = 0;
                    break;
                }

                out = hvm_exec_command(cmd);
                if (!out) out = strdup("");
                if (!out || !hvm_push_string(vm, out)) {
                    free(cmd);
                    free(out);
                    hvm_free_value(&cmd_value);
                    hvm_set_error_msg(vm, "Stack overflow in EXEC_COMMAND");
                    vm->running = 0;
                    break;
                }

                free(cmd);
                free(out);
                hvm_free_value(&cmd_value);
                break;
            }

            case HVM_JUMP: {
                int64_t target = instr->operand.int_operand;
                if (target < 0 || (size_t)target >= vm->instruction_count) {
                    hvm_set_error_msg(vm, "Invalid jump target");
                    vm->running = 0;
                } else {
                    vm->pc = (size_t)target;
                    continue;
                }
                break;
            }

            case HVM_JUMP_IF_FALSE:
            case HVM_JUMP_IF_TRUE: {
                HVM_Value cond;
                int truthy;
                int should_jump;
                int64_t target = instr->operand.int_operand;
                if (vm->stack_top < 1) {
                    hvm_set_error_msg(vm, "Stack underflow in conditional jump");
                    vm->running = 0;
                    break;
                }
                cond = hvm_pop(vm);
                truthy = hvm_is_truthy(cond);
                should_jump = (instr->opcode == HVM_JUMP_IF_FALSE) ? !truthy : truthy;
                hvm_free_value(&cond);
                if (should_jump) {
                    if (target < 0 || (size_t)target >= vm->instruction_count) {
                        hvm_set_error_msg(vm, "Invalid jump target");
                        vm->running = 0;
                    } else {
                        vm->pc = (size_t)target;
                        continue;
                    }
                }
                break;
            }

            case HVM_CALL: {
                int64_t target = instr->operand.int_operand;
                if (target < 0 || (size_t)target >= vm->instruction_count) {
                    hvm_set_error_msg(vm, "Invalid call target");
                    vm->running = 0;
                    break;
                }
                if (vm->call_top >= HVM_CALL_STACK_SIZE) {
                    hvm_set_error_msg(vm, "Call stack overflow");
                    vm->running = 0;
                    break;
                }
                vm->call_stack[vm->call_top++] = vm->pc + 1;
                vm->pc = (size_t)target;
                continue;
            }

            case HVM_RETURN:
                if (vm->call_top == 0) {
                    vm->running = 0;
                } else {
                    vm->pc = vm->call_stack[--vm->call_top];
                    continue;
                }
                break;

            case HVM_HALT:
                vm->running = 0;
                break;

            case HVM_NOP:
                break;

            default:
                hvm_set_error_msg(vm, "Unknown opcode");
                vm->running = 0;
                break;
        }
        if (vm->gc_pending) {
            hvm_gc_collect_internal(vm);
        }

        vm->pc++;
    }

    if (!vm->error_message && g_gui.using_real && g_gui.running) {
        if (!g_gui.loop_called) {
            GUI_LOG("[GUI] Close the window to exit.\n");
        }
        gui_run_loop_until_close();
    }

    return vm->error_message ? 0 : 1;
}

void hvm_gc_collect(HVM_VM* vm) {
    hvm_gc_collect_internal(vm);
}

void hvm_gc_set_enabled(HVM_VM* vm, int enabled) {
    if (!vm) return;
    vm->gc_enabled = enabled ? 1 : 0;
    if (!vm->gc_enabled) {
        vm->gc_pending = 0;
    } else if (vm->gc_bytes >= vm->gc_next_collection) {
        vm->gc_pending = 1;
    }
}

size_t hvm_gc_live_objects(HVM_VM* vm) {
    return vm ? vm->gc_object_count : 0;
}

size_t hvm_gc_live_bytes(HVM_VM* vm) {
    return vm ? vm->gc_bytes : 0;
}
void hvm_set_error(HVM_VM* vm, int code, const char* message) {
    if (!vm) return;
    vm->error_code = code;
    hvm_set_error_msg(vm, message);
}

const char* hvm_get_error(HVM_VM* vm) {
    return vm ? vm->error_message : NULL;
}

void hvm_print_stack(HVM_VM* vm) {
    size_t i;
    if (!vm) return;
    printf("Stack (top=%zu):\n", vm->stack_top);
    for (i = 0; i < vm->stack_top; i++) {
        HVM_Value* v = &vm->stack[i];
        printf("  [%zu]: ", i);
        switch (v->type) {
            case HVM_TYPE_INT: printf("INT %lld", (long long)v->data.int_value); break;
            case HVM_TYPE_FLOAT: printf("FLOAT %g", v->data.float_value); break;
            case HVM_TYPE_STRING: printf("STRING %s", v->data.string_value ? v->data.string_value : ""); break;
            case HVM_TYPE_BOOL: printf("BOOL %s", v->data.bool_value ? "true" : "false"); break;
            default: printf("NULL"); break;
        }
        printf("\n");
    }
}

void hvm_print_instructions(HVM_VM* vm) {
    size_t i;
    if (!vm || !vm->instructions) return;
    printf("Instructions:\n");
    for (i = 0; i < vm->instruction_count; i++) {
        HVM_Instruction* instr = &vm->instructions[i];
        printf("  [%zu]: opcode=%d\n", i, instr->opcode);
    }
}

void hvm_disassemble(HVM_VM* vm) {
    hvm_print_instructions(vm);
}














































































