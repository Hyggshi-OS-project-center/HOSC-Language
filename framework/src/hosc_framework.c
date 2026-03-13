/* hosc_framework.c - HOSC source file */

#include "hosc_runtime.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#include <windows.h>
#else
#include <time.h>
#endif

#define MAX_INSTRUCTIONS 2048
#define MAX_LOOP_DEPTH 64
#define MAX_FRAME_EVENTS 256

typedef enum {
    OP_WINDOW = 0,
    OP_TEXT,
    OP_WIN32_MESSAGE_BOX,
    OP_PUMP_EVENTS,
    OP_EVENT_CLICK,
    OP_EVENT_KEY,
    OP_EVENT_MOUSE_MOVE,
    OP_LOOP_SIMPLE,
    OP_LOOP_BEGIN,
    OP_LOOP_END
} OpCode;

typedef struct {
    OpCode opcode;
    int a;
    int b;
    int c;
    int jump_index;
    char text[512];
} Instruction;

typedef struct {
    Instruction items[MAX_INSTRUCTIONS];
    int count;
} Program;

static const char* skip_spaces(const char* p) {
    while (*p && isspace((unsigned char)*p)) {
        p++;
    }
    return p;
}

static int starts_with(const char* s, const char* prefix) {
    return strncmp(s, prefix, strlen(prefix)) == 0;
}

static int parse_quoted_string(const char** cursor, char* out, size_t out_cap) {
    const char* p = skip_spaces(*cursor);
    size_t i = 0;

    if (*p != '"') {
        return 0;
    }
    p++;

    while (*p && *p != '"') {
        char ch = *p;
        if (ch == '\\' && p[1]) {
            p++;
            if (*p == 'n') {
                ch = '\n';
            } else if (*p == 't') {
                ch = '\t';
            } else {
                ch = *p;
            }
        }

        if (i + 1 >= out_cap) {
            return 0;
        }

        out[i++] = ch;
        p++;
    }

    if (*p != '"') {
        return 0;
    }

    out[i] = '\0';
    p++;
    *cursor = p;
    return 1;
}

static int parse_char(const char** cursor, char expected) {
    const char* p = skip_spaces(*cursor);

    if (*p != expected) {
        return 0;
    }

    *cursor = p + 1;
    return 1;
}

static int parse_int(const char** cursor, int* out_value) {
    const char* p = skip_spaces(*cursor);
    char* end_ptr = NULL;
    long value;

    value = strtol(p, &end_ptr, 10);
    if (p == end_ptr) {
        return 0;
    }

    *out_value = (int)value;
    *cursor = end_ptr;
    return 1;
}

static int parse_statement_end(const char** cursor) {
    const char* p = skip_spaces(*cursor);

    if (*p == ';') {
        p++;
    }

    p = skip_spaces(p);
    return (*p == '\0');
}

static int parse_loop_block_end(const char** cursor) {
    const char* p = skip_spaces(*cursor);
    return (*p == '\0');
}

static int add_instruction(Program* program, const Instruction* inst) {
    if (program->count >= MAX_INSTRUCTIONS) {
        return 0;
    }

    program->items[program->count++] = *inst;
    return 1;
}

static void sleep_ms(int ms) {
    if (ms <= 0) {
        return;
    }

#ifdef _WIN32
    Sleep((DWORD)ms);
#else
    struct timespec req;
    req.tv_sec = ms / 1000;
    req.tv_nsec = (long)(ms % 1000) * 1000000L;
    nanosleep(&req, NULL);
#endif
}

static int parse_window_statement(const char* line, int line_no, Program* program) {
    const char* p = line + strlen("window");
    Instruction inst;

    memset(&inst, 0, sizeof(inst));
    inst.opcode = OP_WINDOW;
    inst.a = 800;
    inst.b = 600;
    inst.jump_index = -1;

    if (!parse_char(&p, '(') || !parse_quoted_string(&p, inst.text, sizeof(inst.text)) ||
        !parse_char(&p, ')') || !parse_statement_end(&p)) {
        fprintf(stderr, "Parse error at line %d: invalid window(\"title\") statement\n", line_no);
        return 0;
    }

    return add_instruction(program, &inst);
}

static int parse_text_statement(const char* line, int line_no, Program* program) {
    const char* p = line + strlen("text");
    Instruction inst;

    memset(&inst, 0, sizeof(inst));
    inst.opcode = OP_TEXT;
    inst.jump_index = -1;

    if (!parse_char(&p, '(') || !parse_int(&p, &inst.a) || !parse_char(&p, ',') || !parse_int(&p, &inst.b) ||
        !parse_char(&p, ',') || !parse_quoted_string(&p, inst.text, sizeof(inst.text)) ||
        !parse_char(&p, ')') || !parse_statement_end(&p)) {
        fprintf(stderr, "Parse error at line %d: invalid text(x, y, \"msg\") statement\n", line_no);
        return 0;
    }

    return add_instruction(program, &inst);
}

static int parse_message_box_statement(const char* line, int line_no, Program* program) {
    const char* p = line + strlen("win32_message_box");
    Instruction inst;

    memset(&inst, 0, sizeof(inst));
    inst.opcode = OP_WIN32_MESSAGE_BOX;
    inst.jump_index = -1;

    if (!parse_char(&p, '(') || !parse_quoted_string(&p, inst.text, sizeof(inst.text)) ||
        !parse_char(&p, ')') || !parse_statement_end(&p)) {
        fprintf(stderr, "Parse error at line %d: invalid win32_message_box(\"msg\") statement\n", line_no);
        return 0;
    }

    return add_instruction(program, &inst);
}

static int parse_pump_statement(const char* line, int line_no, Program* program) {
    const char* p = line + strlen("pump_events");
    Instruction inst;

    memset(&inst, 0, sizeof(inst));
    inst.opcode = OP_PUMP_EVENTS;
    inst.jump_index = -1;

    if (!parse_char(&p, '(') || !parse_char(&p, ')') || !parse_statement_end(&p)) {
        fprintf(stderr, "Parse error at line %d: invalid pump_events() statement\n", line_no);
        return 0;
    }

    return add_instruction(program, &inst);
}

static int parse_on_click_statement(const char* line, int line_no, Program* program) {
    const char* p = line + strlen("on_click");
    Instruction inst;

    memset(&inst, 0, sizeof(inst));
    inst.opcode = OP_EVENT_CLICK;
    inst.jump_index = -1;

    if (!parse_char(&p, '(') || !parse_int(&p, &inst.a) || !parse_char(&p, ',') || !parse_int(&p, &inst.b) ||
        !parse_char(&p, ',') || !parse_quoted_string(&p, inst.text, sizeof(inst.text)) ||
        !parse_char(&p, ')') || !parse_statement_end(&p)) {
        fprintf(stderr, "Parse error at line %d: invalid on_click(x, y, \"msg\") statement\n", line_no);
        return 0;
    }

    return add_instruction(program, &inst);
}

static int parse_on_key_statement(const char* line, int line_no, Program* program) {
    const char* p = line + strlen("on_key");
    Instruction inst;

    memset(&inst, 0, sizeof(inst));
    inst.opcode = OP_EVENT_KEY;
    inst.jump_index = -1;

    if (!parse_char(&p, '(') || !parse_int(&p, &inst.c) || !parse_char(&p, ',') || !parse_int(&p, &inst.a) ||
        !parse_char(&p, ',') || !parse_int(&p, &inst.b) || !parse_char(&p, ',') ||
        !parse_quoted_string(&p, inst.text, sizeof(inst.text)) || !parse_char(&p, ')') ||
        !parse_statement_end(&p)) {
        fprintf(stderr, "Parse error at line %d: invalid on_key(key, x, y, \"msg\") statement\n", line_no);
        return 0;
    }

    return add_instruction(program, &inst);
}

static int parse_on_mouse_move_statement(const char* line, int line_no, Program* program) {
    const char* p = line + strlen("on_mouse_move");
    Instruction inst;

    memset(&inst, 0, sizeof(inst));
    inst.opcode = OP_EVENT_MOUSE_MOVE;
    inst.jump_index = -1;

    if (!parse_char(&p, '(') || !parse_int(&p, &inst.a) || !parse_char(&p, ',') || !parse_int(&p, &inst.b) ||
        !parse_char(&p, ',') || !parse_quoted_string(&p, inst.text, sizeof(inst.text)) ||
        !parse_char(&p, ')') || !parse_statement_end(&p)) {
        fprintf(stderr, "Parse error at line %d: invalid on_mouse_move(x, y, \"msg\") statement\n", line_no);
        return 0;
    }

    return add_instruction(program, &inst);
}

static int parse_loop_statement(const char* line, int line_no, Program* program) {
    const char* p = line + strlen("loop");
    Instruction inst;
    int has_args = 0;

    memset(&inst, 0, sizeof(inst));
    inst.opcode = OP_LOOP_SIMPLE;
    inst.a = 0;
    inst.b = 16;
    inst.jump_index = -1;

    if (!parse_char(&p, '(')) {
        fprintf(stderr, "Parse error at line %d: invalid loop() statement\n", line_no);
        return 0;
    }

    p = skip_spaces(p);
    if (*p != ')') {
        has_args = 1;
    }

    if (has_args) {
        if (!parse_int(&p, &inst.a)) {
            fprintf(stderr, "Parse error at line %d: invalid loop(frames, sleep_ms) statement\n", line_no);
            return 0;
        }

        p = skip_spaces(p);
        if (*p == ',') {
            if (!parse_char(&p, ',') || !parse_int(&p, &inst.b)) {
                fprintf(stderr, "Parse error at line %d: invalid loop(frames, sleep_ms) statement\n", line_no);
                return 0;
            }
        }
    }

    if (!parse_char(&p, ')') || !parse_statement_end(&p)) {
        fprintf(stderr, "Parse error at line %d: invalid loop() statement\n", line_no);
        return 0;
    }

    return add_instruction(program, &inst);
}

static int parse_loop_begin(const char* line, int line_no, Program* program, int* loop_stack, int* loop_depth) {
    const char* p = line + strlen("loop");
    Instruction inst;
    int begin_index;

    if (*loop_depth >= MAX_LOOP_DEPTH) {
        fprintf(stderr, "Parse error at line %d: loop nesting too deep\n", line_no);
        return 0;
    }

    memset(&inst, 0, sizeof(inst));
    inst.opcode = OP_LOOP_BEGIN;
    inst.jump_index = -1;

    if (!parse_char(&p, '(') || !parse_int(&p, &inst.a) || !parse_char(&p, ',') || !parse_int(&p, &inst.b) ||
        !parse_char(&p, ')') || !parse_char(&p, '{') || !parse_loop_block_end(&p)) {
        fprintf(stderr, "Parse error at line %d: invalid loop(frames, sleep_ms) { statement\n", line_no);
        return 0;
    }

    begin_index = program->count;
    if (!add_instruction(program, &inst)) {
        return 0;
    }

    loop_stack[*loop_depth] = begin_index;
    (*loop_depth)++;
    return 1;
}

static int parse_loop_end(const char* line, int line_no, Program* program, int* loop_stack, int* loop_depth) {
    const char* p = line + 1;
    Instruction inst;
    int begin_index;
    int end_index;

    if (!parse_loop_block_end(&p)) {
        fprintf(stderr, "Parse error at line %d: invalid closing brace\n", line_no);
        return 0;
    }

    if (*loop_depth <= 0) {
        fprintf(stderr, "Parse error at line %d: unexpected '}'\n", line_no);
        return 0;
    }

    (*loop_depth)--;
    begin_index = loop_stack[*loop_depth];

    memset(&inst, 0, sizeof(inst));
    inst.opcode = OP_LOOP_END;
    inst.jump_index = begin_index;

    end_index = program->count;
    if (!add_instruction(program, &inst)) {
        return 0;
    }

    program->items[begin_index].jump_index = end_index;
    return 1;
}

static int parse_script_file(const char* path, Program* program) {
    FILE* file;
    char line[1024];
    int line_no = 0;
    int loop_stack[MAX_LOOP_DEPTH];
    int loop_depth = 0;
    int func_depth = 0;

    file = fopen(path, "r");
    if (!file) {
        fprintf(stderr, "Cannot open script: %s\n", path);
        return 0;
    }

    program->count = 0;

    while (fgets(line, sizeof(line), file)) {
        char* p = line;
        const char* loop_paren = NULL;
        const char* loop_close = NULL;
        const char* loop_brace = NULL;

        line_no++;

        while (*p && isspace((unsigned char)*p)) {
            p++;
        }

        if (*p == '\0' || *p == '#' || (p[0] == '/' && p[1] == '/')) {
            continue;
        }

        if (starts_with(p, "package")) {
            continue;
        }

        if (starts_with(p, "func")) {
            char* open_brace = strchr(p, '{');
            if (open_brace) {
                func_depth++;
                continue;
            }
        }

        if (*p == '}') {
            if (loop_depth > 0) {
                if (!parse_loop_end(p, line_no, program, loop_stack, &loop_depth)) {
                    fclose(file);
                    return 0;
                }
                continue;
            }

            if (func_depth > 0) {
                func_depth--;
                continue;
            }
        }

        if (starts_with(p, "window")) {
            if (!parse_window_statement(p, line_no, program)) {
                fclose(file);
                return 0;
            }
            continue;
        }

        if (starts_with(p, "text")) {
            if (!parse_text_statement(p, line_no, program)) {
                fclose(file);
                return 0;
            }
            continue;
        }

        if (starts_with(p, "win32_message_box")) {
            if (!parse_message_box_statement(p, line_no, program)) {
                fclose(file);
                return 0;
            }
            continue;
        }

        if (starts_with(p, "pump_events")) {
            if (!parse_pump_statement(p, line_no, program)) {
                fclose(file);
                return 0;
            }
            continue;
        }

        if (starts_with(p, "on_click")) {
            if (!parse_on_click_statement(p, line_no, program)) {
                fclose(file);
                return 0;
            }
            continue;
        }

        if (starts_with(p, "on_key")) {
            if (!parse_on_key_statement(p, line_no, program)) {
                fclose(file);
                return 0;
            }
            continue;
        }

        if (starts_with(p, "on_mouse_move")) {
            if (!parse_on_mouse_move_statement(p, line_no, program)) {
                fclose(file);
                return 0;
            }
            continue;
        }

        if (starts_with(p, "loop")) {
            loop_paren = strchr(p, '(');
            loop_close = loop_paren ? strchr(loop_paren, ')') : NULL;
            loop_brace = loop_close ? strchr(loop_close, '{') : NULL;

            if (loop_brace) {
                if (!parse_loop_begin(p, line_no, program, loop_stack, &loop_depth)) {
                    fclose(file);
                    return 0;
                }
            } else {
                if (!parse_loop_statement(p, line_no, program)) {
                    fclose(file);
                    return 0;
                }
            }
            continue;
        }

        fprintf(stderr, "Unsupported statement at line %d: %s", line_no, p);
        fclose(file);
        return 0;
    }

    fclose(file);

    if (loop_depth != 0) {
        fprintf(stderr, "Parse error: missing closing brace for loop block\n");
        return 0;
    }

    if (func_depth != 0) {
        fprintf(stderr, "Parse error: missing closing brace for function block\n");
        return 0;
    }

    return 1;
}

static int collect_frame_events(HOSCRuntimeContext* context, HOSCGUIEvent* events, int max_events, int* quit_requested) {
    int count = 0;
    HOSCGUIEvent event;

    while (count < max_events && hosc_gui_poll_event(context, &event)) {
        events[count++] = event;
        if (event.type == HOSC_GUI_EVENT_QUIT) {
            *quit_requested = 1;
        }
    }

    return count;
}

static void handle_click_events(HOSCRuntimeContext* context, const Instruction* inst, const HOSCGUIEvent* events, int count, int* touched_gui) {
    int i;

    for (i = 0; i < count; i++) {
        if (events[i].type == HOSC_GUI_EVENT_MOUSE_DOWN) {
            char message[768];
            snprintf(message, sizeof(message), "%s (button=%d, x=%d, y=%d)",
                     inst->text, events[i].mouse_button, events[i].mouse_x, events[i].mouse_y);
            hosc_gui_draw_text(context, inst->a, inst->b, message);
            *touched_gui = 1;
        }
    }
}

static void handle_key_events(HOSCRuntimeContext* context, const Instruction* inst, const HOSCGUIEvent* events, int count, int* touched_gui) {
    int i;

    for (i = 0; i < count; i++) {
        if (events[i].type == HOSC_GUI_EVENT_KEY_DOWN) {
            if (inst->c == -1 || inst->c == events[i].key_code) {
                char message[768];
                snprintf(message, sizeof(message), "%s (key=%d)", inst->text, events[i].key_code);
                hosc_gui_draw_text(context, inst->a, inst->b, message);
                *touched_gui = 1;
            }
        }
    }
}

static void handle_mouse_move_events(HOSCRuntimeContext* context, const Instruction* inst, const HOSCGUIEvent* events, int count, int* touched_gui) {
    int i;

    for (i = 0; i < count; i++) {
        if (events[i].type == HOSC_GUI_EVENT_MOUSE_MOVE) {
            char message[768];
            snprintf(message, sizeof(message), "%s (x=%d, y=%d)", inst->text, events[i].mouse_x, events[i].mouse_y);
            hosc_gui_draw_text(context, inst->a, inst->b, message);
            *touched_gui = 1;
        }
    }
}

static int execute_program(HOSCRuntimeContext* context, const Program* program, int* touched_gui) {
    int pc = 0;
    int should_quit = 0;
    int loop_remaining[MAX_INSTRUCTIONS];
    int frame_events_valid = 0;
    int frame_event_count = 0;
    HOSCGUIEvent frame_events[MAX_FRAME_EVENTS];

    memset(loop_remaining, 0xFF, sizeof(loop_remaining));

    while (pc < program->count && !should_quit) {
        const Instruction* inst = &program->items[pc];

        if (*touched_gui && hosc_gui_backend(context) == HOSC_GUI_BACKEND_WIN32 && !hosc_gui_is_running(context)) {
            should_quit = 1;
            break;
        }

        switch (inst->opcode) {
            case OP_WINDOW:
                hosc_gui_create_window(context, inst->text, inst->a, inst->b);
                *touched_gui = 1;
                frame_events_valid = 0;
                pc++;
                break;
            case OP_TEXT:
                hosc_gui_draw_text(context, inst->a, inst->b, inst->text);
                *touched_gui = 1;
                pc++;
                break;
            case OP_WIN32_MESSAGE_BOX:
                hosc_gui_message_box(context, inst->text);
                pc++;
                break;
            case OP_PUMP_EVENTS:
                frame_event_count = collect_frame_events(context, frame_events, MAX_FRAME_EVENTS, &should_quit);
                frame_events_valid = 1;
                pc++;
                break;
            case OP_EVENT_CLICK:
                if (!frame_events_valid) {
                    frame_event_count = collect_frame_events(context, frame_events, MAX_FRAME_EVENTS, &should_quit);
                    frame_events_valid = 1;
                }
                handle_click_events(context, inst, frame_events, frame_event_count, touched_gui);
                pc++;
                break;
            case OP_EVENT_KEY:
                if (!frame_events_valid) {
                    frame_event_count = collect_frame_events(context, frame_events, MAX_FRAME_EVENTS, &should_quit);
                    frame_events_valid = 1;
                }
                handle_key_events(context, inst, frame_events, frame_event_count, touched_gui);
                pc++;
                break;
            case OP_EVENT_MOUSE_MOVE:
                if (!frame_events_valid) {
                    frame_event_count = collect_frame_events(context, frame_events, MAX_FRAME_EVENTS, &should_quit);
                    frame_events_valid = 1;
                }
                handle_mouse_move_events(context, inst, frame_events, frame_event_count, touched_gui);
                pc++;
                break;
            case OP_LOOP_SIMPLE: {
                int max_frames = inst->a;
                int sleep_duration = (inst->b > 0 ? inst->b : 16);
                int frame_count = 0;

                if (!*touched_gui) {
                    sleep_ms(sleep_duration);
                    pc++;
                    break;
                }

                while (!should_quit) {
                    frame_event_count = collect_frame_events(context, frame_events, MAX_FRAME_EVENTS, &should_quit);
                    frame_events_valid = 1;

                    if (should_quit) {
                        break;
                    }

                    if (hosc_gui_backend(context) == HOSC_GUI_BACKEND_WIN32 && !hosc_gui_is_running(context)) {
                        should_quit = 1;
                        break;
                    }

                    if (max_frames > 0) {
                        frame_count++;
                        if (frame_count >= max_frames) {
                            break;
                        }
                    }

                    sleep_ms(sleep_duration);
                }

                frame_events_valid = 0;
                pc++;
                break;
            }
            case OP_LOOP_BEGIN:
                if (loop_remaining[pc] == -1) {
                    if (inst->a <= 0) {
                        loop_remaining[pc] = -2;
                    } else {
                        loop_remaining[pc] = inst->a;
                    }
                }

                if (loop_remaining[pc] == 0) {
                    loop_remaining[pc] = -1;
                    pc = inst->jump_index + 1;
                    break;
                }

                frame_event_count = collect_frame_events(context, frame_events, MAX_FRAME_EVENTS, &should_quit);
                frame_events_valid = 1;
                pc++;
                break;
            case OP_LOOP_END: {
                int begin_index = inst->jump_index;
                int remaining;
                int sleep_duration;

                if (begin_index < 0 || begin_index >= program->count) {
                    fprintf(stderr, "Runtime error: invalid loop jump target\n");
                    return 0;
                }

                remaining = loop_remaining[begin_index];
                sleep_duration = program->items[begin_index].b;

                frame_events_valid = 0;

                if (remaining == -2) {
                    sleep_ms(sleep_duration);
                    pc = begin_index;
                    break;
                }

                if (remaining > 1) {
                    loop_remaining[begin_index] = remaining - 1;
                    sleep_ms(sleep_duration);
                    pc = begin_index;
                    break;
                }

                loop_remaining[begin_index] = -1;
                pc++;
                break;
            }
            default:
                fprintf(stderr, "Runtime error: unsupported opcode %d\n", (int)inst->opcode);
                return 0;
        }
    }

    return 1;
}

static void print_usage(const char* exe_name) {
    printf("Usage:\n");
    printf("  %s run <script.hosc>\n", exe_name);
    printf("\n");
    printf("Statements:\n");
    printf("  window(\"Title\");\n");
    printf("  text(x, y, \"Message\");\n");
    printf("  win32_message_box(\"Message\");\n");
    printf("  pump_events();\n");
    printf("  on_click(x, y, \"Message\");\n");
    printf("  on_key(key_code, x, y, \"Message\");\n");
    printf("  on_mouse_move(x, y, \"Message\");\n");
    printf("  loop();\n");
    printf("  loop(frames, sleep_ms);\n");
    printf("  loop(frames, sleep_ms) { ... }\n");
    printf("  package main / func main() { ... } wrappers are supported.\n");
    printf("    frames <= 0 means infinite loop until window close event.\n");
}

int main(int argc, char** argv) {
    HOSCRuntimeConfig config;
    HOSCRuntimeContext* runtime;
    Program program;
    int touched_gui = 0;

    if (argc < 3 || strcmp(argv[1], "run") != 0) {
        print_usage(argv[0]);
        return 1;
    }

    config.enable_debug = false;
    config.enable_memory_tracking = true;
    config.enable_garbage_collection = false;
    config.max_memory_mb = 256;
    config.log_file = NULL;

    runtime = hosc_runtime_init(&config);
    if (!runtime) {
        fprintf(stderr, "Failed to initialize HOSC runtime\n");
        return 1;
    }

    hosc_load_module(runtime, "core");
    hosc_load_module(runtime, "gui");
    hosc_load_module(runtime, "win32");

    printf("HOSC framework runtime started (GUI backend: %s)\n", hosc_gui_backend_name(runtime));

    if (!parse_script_file(argv[2], &program)) {
        hosc_runtime_shutdown(runtime);
        return 1;
    }

    if (!execute_program(runtime, &program, &touched_gui)) {
        hosc_runtime_shutdown(runtime);
        return 1;
    }

#ifdef _WIN32
    if (touched_gui && hosc_gui_backend(runtime) == HOSC_GUI_BACKEND_WIN32 && hosc_gui_is_running(runtime)) {
        printf("Press Enter to close GUI window...\n");
        getchar();
    }
#endif

    hosc_runtime_shutdown(runtime);
    return 0;
}





