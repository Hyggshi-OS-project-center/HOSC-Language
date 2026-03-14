/* executor.c - AST execution via HVM pipeline (legacy API compatibility) */

#include <stdio.h>
#include <stdlib.h>

#include "ast.h"
#include "hvm.h"
#include "hvm_compiler.h"
#include "hvm_platform.h"

void runtime_execute(ASTNode *ast) {
    HVM_VM *vm;
    HVM_Compiler *compiler;

    if (!ast) return;

    vm = hvm_create();
    if (!vm) {
        fprintf(stderr, "Runtime error: failed to create HVM VM\n");
        return;
    }

    compiler = hvm_compiler_create(vm);
    if (!compiler) {
        fprintf(stderr, "Runtime error: failed to create HVM compiler\n");
        hvm_destroy(vm);
        return;
    }

    if (!hvm_compiler_compile_ast(compiler, ast) || hvm_compiler_has_errors(compiler)) {
        hvm_compiler_print_errors(compiler);
        hvm_compiler_destroy(compiler);
        hvm_destroy(vm);
        return;
    }

    hvm_platform_hide_console_if_needed(vm->instructions, vm->instruction_count);

    if (!hvm_run(vm)) {
        const char *msg = hvm_get_error(vm);
        fprintf(stderr, "VM execution failed%s%s\n", msg ? ": " : "", msg ? msg : "");
    }

    hvm_compiler_destroy(compiler);
    hvm_destroy(vm);
}

void runtime_execute_error(const char *message) {
    fprintf(stderr, "[ERROR] %s\n", message ? message : "");
}

void runtime_execute_info(const char *message) {
    fprintf(stdout, "[INFO] %s\n", message ? message : "");
}

void runtime_execute_warning(const char *message) {
    fprintf(stderr, "[WARN] %s\n", message ? message : "");
}

void runtime_execute_yesno(const char *message) {
    fprintf(stdout, "[PROMPT] %s (y/n)\n", message ? message : "");
}

void runtime_execute_message_box(const char *message) {
    fprintf(stdout, "[MESSAGE] %s\n", message ? message : "");
}

void runtime_execute_sleep(int milliseconds) {
    hvm_platform_sleep_ms(milliseconds);
}

void runtime_execute_create_window(const char *title, const char *message) {
    (void)title;
    (void)message;
    fprintf(stdout, "[GUI] create_window not available in legacy runtime\n");
}

void runtime_execute_print(const char *message) {
    fprintf(stdout, "%s\n", message ? message : "");
}

void runtime_execute_file_dialog(const char *title, const char *filter) {
    (void)title;
    (void)filter;
    fprintf(stdout, "[GUI] file_dialog not available in legacy runtime\n");
}

void runtime_execute_color_dialog(int red, int green, int blue) {
    (void)red; (void)green; (void)blue;
    fprintf(stdout, "[GUI] color_dialog not available in legacy runtime\n");
}

void runtime_execute_font_dialog(const char *font_name, int font_size) {
    (void)font_name; (void)font_size;
    fprintf(stdout, "[GUI] font_dialog not available in legacy runtime\n");
}

void runtime_execute_open_url(const char *url) {
    fprintf(stdout, "[OPEN] %s\n", url ? url : "");
}

void runtime_execute_beep(int frequency, int duration) {
    (void)frequency; (void)duration;
    fprintf(stdout, "[BEEP]\n");
}

void runtime_execute_get_screen_size(int *width, int *height) {
    if (width) *width = 0;
    if (height) *height = 0;
}

void runtime_execute_get_cursor_pos(int *x, int *y) {
    if (x) *x = 0;
    if (y) *y = 0;
}

void runtime_execute_set_cursor_pos(int x, int y) {
    (void)x; (void)y;
}

void runtime_execute_get_clipboard_text(char **text) {
    if (text) *text = NULL;
}

void runtime_execute_set_clipboard_text(const char *text) {
    (void)text;
}

void runtime_execute_get_system_info(const char *info) {
    (void)info;
}

void runtime_execute_get_time(const char *format) {
    (void)format;
}
