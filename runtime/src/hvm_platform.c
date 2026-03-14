/* hvm_platform.c - Platform helpers for HOSC VM runtime */

#include "hvm_platform.h"

#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <time.h>

#ifdef _WIN32
#include <windows.h>
#endif

static int str_eq_ci(const char *a, const char *b) {
    unsigned char ca;
    unsigned char cb;

    if (!a || !b) return 0;
    while (*a && *b) {
        ca = (unsigned char)*a;
        cb = (unsigned char)*b;
        if (tolower(ca) != tolower(cb)) return 0;
        a++;
        b++;
    }

    return *a == '\0' && *b == '\0';
}

static int is_gui_native_name(const char *name) {
    if (!name) return 0;
    return strncmp(name, "gui.", 4) == 0;
}

static int bytecode_has_gui_opcodes(const HVM_Instruction *code, size_t count) {
    size_t i;
    if (!code) return 0;

    for (i = 0; i < count; i++) {
        HVM_Opcode op = code[i].opcode;
        if (op == HVM_CALL_NATIVE && is_gui_native_name(code[i].operand.string_operand)) {
            return 1;
        }
        if (op == HVM_CREATE_WINDOW ||
            op == HVM_DRAW_TEXT ||
            op == HVM_DRAW_BUTTON ||
            op == HVM_DRAW_BUTTON_STATE ||
            op == HVM_DRAW_INPUT ||
            op == HVM_DRAW_INPUT_STATE ||
            op == HVM_DRAW_TEXTAREA ||
            op == HVM_DRAW_IMAGE ||
            op == HVM_MENU_SETUP_NOTEPAD ||
            op == HVM_LOOP) {
            return 1;
        }
    }

    return 0;
}

static int env_force_console(void) {
    const char *env = getenv("HVM_FORCE_CONSOLE");
    if (!env || !env[0]) return 0;
    return strcmp(env, "1") == 0 || str_eq_ci(env, "true") || str_eq_ci(env, "yes");
}

void hvm_platform_hide_console_if_needed(const HVM_Instruction *code, size_t count) {
#ifdef _WIN32
    HWND console_hwnd;
    if (env_force_console()) return;
    if (!bytecode_has_gui_opcodes(code, count)) return;

    console_hwnd = GetConsoleWindow();
    if (console_hwnd) {
        ShowWindow(console_hwnd, SW_HIDE);
    }
#else
    (void)code;
    (void)count;
#endif
}

void hvm_platform_sleep_ms(int ms) {
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
