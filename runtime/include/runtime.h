#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#include "ast.h"

/**
 * Execute an AST node directly in the runtime
 * This bypasses code generation and runs the code immediately
 */
void runtime_execute(ASTNode *ast);

/**
 * Runtime execution functions for individual node types
 */
void runtime_execute_error(const char *message);
void runtime_execute_info(const char *message);
void runtime_execute_warning(const char *message);
void runtime_execute_yesno(const char *message);
void runtime_execute_message_box(const char *message);
void runtime_execute_sleep(int milliseconds);
void runtime_execute_create_window(const char *title, const char *message);
void runtime_execute_print(const char *message);
void runtime_execute_file_dialog(const char *title, const char *filter);
void runtime_execute_color_dialog(int red, int green, int blue);
void runtime_execute_font_dialog(const char *font_name, int font_size);
void runtime_execute_open_url(const char *url);
void runtime_execute_beep(int frequency, int duration);
void runtime_execute_get_screen_size(int *width, int *height);
void runtime_execute_get_cursor_pos(int *x, int *y);
void runtime_execute_set_cursor_pos(int x, int y);
void runtime_execute_get_clipboard_text(char **text);
void runtime_execute_set_clipboard_text(const char *text);
void runtime_execute_get_system_info(const char *info);
void runtime_execute_get_time(const char *format);

#ifdef __cplusplus
}
#endif
