/* runtime_services.c - Default runtime service wiring for HOSC VM */

#include <stdlib.h>
#include <string.h>

#include "runtime_services.h"
#include "runtime_gui.h"
#include "hvm_platform.h"

typedef struct {
    HVM_GuiRuntime *gui;
} DefaultRuntimeState;

typedef struct {
    const char *name;
    HVM_Opcode opcode;
} NativeOpcodeBinding;

static const NativeOpcodeBinding k_gui_bindings[] = {
    {"gui.create_window", HVM_CREATE_WINDOW},
    {"gui.clear", HVM_CLEAR},
    {"gui.set_bg_color", HVM_SET_BG_COLOR},
    {"gui.set_color", HVM_SET_COLOR},
    {"gui.set_font_size", HVM_SET_FONT_SIZE},
    {"gui.draw_text", HVM_DRAW_TEXT},
    {"gui.draw_button", HVM_DRAW_BUTTON},
    {"gui.draw_button_state", HVM_DRAW_BUTTON_STATE},
    {"gui.draw_input", HVM_DRAW_INPUT},
    {"gui.draw_input_state", HVM_DRAW_INPUT_STATE},
    {"gui.draw_textarea", HVM_DRAW_TEXTAREA},
    {"gui.draw_image", HVM_DRAW_IMAGE},
    {"gui.get_mouse_x", HVM_GET_MOUSE_X},
    {"gui.get_mouse_y", HVM_GET_MOUSE_Y},
    {"gui.is_mouse_down", HVM_IS_MOUSE_DOWN},
    {"gui.was_mouse_up", HVM_WAS_MOUSE_UP},
    {"gui.was_mouse_click", HVM_WAS_MOUSE_CLICK},
    {"gui.is_mouse_hover", HVM_IS_MOUSE_HOVER},
    {"gui.is_key_down", HVM_IS_KEY_DOWN},
    {"gui.was_key_press", HVM_WAS_KEY_PRESS},
    {"gui.delta_time", HVM_DELTA_TIME},
    {"gui.layout_reset", HVM_LAYOUT_RESET},
    {"gui.layout_next", HVM_LAYOUT_NEXT},
    {"gui.layout_row", HVM_LAYOUT_ROW},
    {"gui.layout_column", HVM_LAYOUT_COLUMN},
    {"gui.layout_grid", HVM_LAYOUT_GRID},
    {"gui.loop", HVM_LOOP},
    {"gui.menu_setup_notepad", HVM_MENU_SETUP_NOTEPAD},
    {"gui.menu_event", HVM_MENU_EVENT},
    {"gui.scroll_set_range", HVM_SCROLL_SET_RANGE},
    {"gui.scroll_y", HVM_SCROLL_Y},
    {"gui.file_open_dialog", HVM_FILE_OPEN_DIALOG},
    {"gui.file_save_dialog", HVM_FILE_SAVE_DIALOG},
    {"gui.input_set", HVM_INPUT_SET},
    {"gui.textarea_set", HVM_TEXTAREA_SET}
};

static HVM_Opcode find_gui_opcode(const char *name) {
    size_t i;
    if (!name) return HVM_OPCODE_COUNT;
    for (i = 0; i < sizeof(k_gui_bindings) / sizeof(k_gui_bindings[0]); i++) {
        if (strcmp(k_gui_bindings[i].name, name) == 0) return k_gui_bindings[i].opcode;
    }
    return HVM_OPCODE_COUNT;
}

static int default_call_native(void *userdata, HVM_VM *vm, const char *name, const HVM_Instruction *instr) {
    DefaultRuntimeState *state = (DefaultRuntimeState *)userdata;
    HVM_Opcode opcode;
    if (!state || !state->gui || !vm || !name) return 0;

    opcode = find_gui_opcode(name);
    if (opcode == HVM_OPCODE_COUNT) {
        return 0;
    }

    return hvm_gui_handle_opcode(state->gui, vm, opcode, instr);
}

static void default_reset_for_run(void *userdata) {
    DefaultRuntimeState *state = (DefaultRuntimeState *)userdata;
    if (!state || !state->gui) return;
    hvm_gui_reset_for_run(state->gui);
}

static void default_post_run(void *userdata, HVM_VM *vm) {
    DefaultRuntimeState *state = (DefaultRuntimeState *)userdata;
    if (!state || !state->gui) return;
    hvm_gui_post_run(state->gui, vm);
}

static void default_sleep_ms(void *userdata, int ms) {
    (void)userdata;
    hvm_platform_sleep_ms(ms);
}

HVM_RuntimeServices* hvm_runtime_services_create_default(void) {
    HVM_RuntimeServices *services = (HVM_RuntimeServices *)calloc(1, sizeof(HVM_RuntimeServices));
    DefaultRuntimeState *state = NULL;

    if (!services) return NULL;

    state = (DefaultRuntimeState *)calloc(1, sizeof(DefaultRuntimeState));
    if (!state) {
        free(services);
        return NULL;
    }

    state->gui = hvm_gui_create();
    if (!state->gui) {
        free(state);
        free(services);
        return NULL;
    }

    services->userdata = state;
    services->call_native = default_call_native;
    services->reset_for_run = default_reset_for_run;
    services->post_run = default_post_run;
    services->sleep_ms = default_sleep_ms;

    return services;
}

void hvm_runtime_services_destroy(HVM_RuntimeServices* services) {
    DefaultRuntimeState *state;
    if (!services) return;
    state = (DefaultRuntimeState *)services->userdata;
    if (state && state->gui) {
        hvm_gui_destroy(state->gui);
        state->gui = NULL;
    }
    free(state);
    services->userdata = NULL;
    free(services);
}

void hvm_runtime_services_reset_for_run(HVM_RuntimeServices* services) {
    if (!services || !services->reset_for_run) return;
    services->reset_for_run(services->userdata);
}

int hvm_runtime_services_call_native(HVM_RuntimeServices* services, HVM_VM* vm, const char* name, const HVM_Instruction* instr) {
    if (!services || !services->call_native) return 0;
    return services->call_native(services->userdata, vm, name, instr);
}

void hvm_runtime_services_post_run(HVM_RuntimeServices* services, HVM_VM* vm) {
    if (!services || !services->post_run) return;
    services->post_run(services->userdata, vm);
}

void hvm_runtime_services_sleep_ms(HVM_RuntimeServices* services, int ms) {
    if (!services || !services->sleep_ms) return;
    services->sleep_ms(services->userdata, ms);
}
