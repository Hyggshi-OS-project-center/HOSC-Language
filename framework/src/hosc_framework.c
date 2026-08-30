/*
 * File: framework\src\hosc_framework.c
 * Purpose: HOSC source file.
 */

/* Expose POSIX time APIs when compiling in strict C99 mode on Unix-like hosts. */
#if !defined(_WIN32) && !defined(_POSIX_C_SOURCE)
#define _POSIX_C_SOURCE 200809L
#endif

#include "hosc_runtime.h"
#include "runtime_gui_backend.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#include <windows.h>
#else
#include <time.h>
#include <unistd.h>
#endif

#define MAX_INSTRUCTIONS 2048
#define MAX_LOOP_DEPTH 64
#define MAX_FRAME_EVENTS 256
#define MAX_WINDOW_STATEMENT 8192
#define WINDOW_FLAG_RESIZABLE 0x01U
#define WINDOW_FLAG_FULLSCREEN 0x02U
#define WINDOW_FLAG_CENTER 0x04U
#define FLAG_LEGACY_CALL 0x80000000U
#define MSG_ICON_INFO 0x00010000U
#define MSG_ICON_WARNING 0x00020000U
#define MSG_ICON_ERROR 0x00040000U
#define EMBEDDED_PROGRAM_MAGIC "HOSC_EMBEDDED_V1"

typedef enum {
    OP_WINDOW = 0,
    OP_PRINT,
    OP_THEME,
    OP_PANEL,
    OP_LABEL,
    OP_MEDIA_PLAYER,
    OP_TEXT,
    OP_RECT,
    OP_IMAGE,
    OP_BUTTON,
    OP_SEEKBAR,
    OP_PLAY_SOUND,
    OP_AUDIO_STOP,
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
    int d;
    int e;
    int jump_index;
    unsigned int flags;
    char text[512];
    char extra[260];
    char meta[512];
    int style[16];
} Instruction;

typedef struct {
    Instruction items[MAX_INSTRUCTIONS];
    int count;
} Program;

typedef struct {
    char magic[24];
    uint32_t version;
    uint32_t reserved;
    uint64_t program_size;
    char base_dir[1024];
} EmbeddedProgramFooter;

typedef struct {
    char magic[16];
    uint32_t version;
    uint32_t program_size;
    char base_dir[1024];
} HBCHeader;

enum {
    THEME_TONE_SURFACE = 0,
    THEME_TONE_SURFACE_2,
    THEME_TONE_ACCENT,
    THEME_TONE_TEXT,
    THEME_TONE_MUTED,
    THEME_TONE_NONE = -1
};

typedef struct {
    int surface[3];
    int surface_2[3];
    int accent[3];
    int text[3];
    int muted[3];
    int radius;
} UITheme;

static UITheme g_ui_theme;

static void reset_ui_theme(void) {
    g_ui_theme.surface[0] = 30;
    g_ui_theme.surface[1] = 36;
    g_ui_theme.surface[2] = 48;
    g_ui_theme.surface_2[0] = 45;
    g_ui_theme.surface_2[1] = 54;
    g_ui_theme.surface_2[2] = 68;
    g_ui_theme.accent[0] = 107;
    g_ui_theme.accent[1] = 192;
    g_ui_theme.accent[2] = 255;
    g_ui_theme.text[0] = 240;
    g_ui_theme.text[1] = 244;
    g_ui_theme.text[2] = 252;
    g_ui_theme.muted[0] = 153;
    g_ui_theme.muted[1] = 165;
    g_ui_theme.muted[2] = 184;
    g_ui_theme.radius = 24;
}

static int parse_tone_string(const char* value) {
    if (strcmp(value, "surface") == 0) {
        return THEME_TONE_SURFACE;
    }
    if (strcmp(value, "surface2") == 0) {
        return THEME_TONE_SURFACE_2;
    }
    if (strcmp(value, "accent") == 0) {
        return THEME_TONE_ACCENT;
    }
    if (strcmp(value, "text") == 0) {
        return THEME_TONE_TEXT;
    }
    if (strcmp(value, "muted") == 0) {
        return THEME_TONE_MUTED;
    }
    if (strcmp(value, "none") == 0) {
        return THEME_TONE_NONE;
    }
    return -999;
}

static void theme_color(int tone, int* r, int* g, int* b) {
    const int* source = g_ui_theme.text;

    switch (tone) {
        case THEME_TONE_SURFACE:
            source = g_ui_theme.surface;
            break;
        case THEME_TONE_SURFACE_2:
            source = g_ui_theme.surface_2;
            break;
        case THEME_TONE_ACCENT:
            source = g_ui_theme.accent;
            break;
        case THEME_TONE_MUTED:
            source = g_ui_theme.muted;
            break;
        case THEME_TONE_TEXT:
        default:
            source = g_ui_theme.text;
            break;
    }

    if (r) {
        *r = source[0];
    }
    if (g) {
        *g = source[1];
    }
    if (b) {
        *b = source[2];
    }
}

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

static int parse_identifier(const char** cursor, char* out, size_t out_cap) {
    const char* p = skip_spaces(*cursor);
    size_t i = 0;

    if (!isalpha((unsigned char)*p) && *p != '_') {
        return 0;
    }

    while ((isalnum((unsigned char)*p) || *p == '_') && i + 1 < out_cap) {
        out[i++] = *p;
        p++;
    }

    if (isalnum((unsigned char)*p) || *p == '_') {
        return 0;
    }

    out[i] = '\0';
    *cursor = p;
    return 1;
}

static int parse_bool(const char** cursor, int* out_value) {
    const char* p = skip_spaces(*cursor);

    if (strncmp(p, "true", 4) == 0 && !(isalnum((unsigned char)p[4]) || p[4] == '_')) {
        *out_value = 1;
        *cursor = p + 4;
        return 1;
    }

    if (strncmp(p, "false", 5) == 0 && !(isalnum((unsigned char)p[5]) || p[5] == '_')) {
        *out_value = 0;
        *cursor = p + 5;
        return 1;
    }

    return 0;
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

static int window_statement_complete(const char* text) {
    int depth = 0;
    int in_string = 0;
    int escape = 0;
    int saw_open = 0;

    while (*text) {
        char ch = *text;

        if (in_string) {
            if (escape) {
                escape = 0;
            } else if (ch == '\\') {
                escape = 1;
            } else if (ch == '"') {
                in_string = 0;
            }
        } else if (ch == '"') {
            in_string = 1;
        } else if (ch == '(') {
            depth++;
            saw_open = 1;
        } else if (ch == ')') {
            if (depth > 0) {
                depth--;
            }
        }

        text++;
    }

    return (saw_open && depth == 0 && !in_string);
}

static int read_window_statement(FILE* file, char* statement, size_t statement_cap, int* line_no) {
    char line[1024];
    size_t used = strlen(statement);

    while (!window_statement_complete(statement)) {
        if (!fgets(line, sizeof(line), file)) {
            return 0;
        }
        (*line_no)++;

        if (used + strlen(line) + 1 >= statement_cap) {
            return 0;
        }

        memcpy(statement + used, line, strlen(line) + 1);
        used += strlen(line);
    }

    return 1;
}

static int parse_window_statement(const char* line, int line_no, Program* program) {
    const char* p = line + strlen("window");
    Instruction inst;

    memset(&inst, 0, sizeof(inst));
    inst.opcode = OP_WINDOW;
    inst.a = 800;
    inst.b = 600;
    inst.jump_index = -1;
    inst.flags = WINDOW_FLAG_RESIZABLE;
    strcpy(inst.text, "HOSC Window");

    if (!parse_char(&p, '(')) {
        fprintf(stderr, "Parse error at line %d: invalid window statement\n", line_no);
        return 0;
    }

    p = skip_spaces(p);
    if (*p == '"') {
        if (!parse_quoted_string(&p, inst.text, sizeof(inst.text)) ||
            !parse_char(&p, ')') || !parse_statement_end(&p)) {
            fprintf(stderr, "Parse error at line %d: invalid window(\"title\") statement\n", line_no);
            return 0;
        }
        return add_instruction(program, &inst);
    }

    if (!parse_char(&p, '{')) {
        fprintf(stderr, "Parse error at line %d: invalid window({ ... }) statement\n", line_no);
        return 0;
    }

    while (1) {
        char property[64];
        int bool_value = 0;

        p = skip_spaces(p);
        if (*p == '}') {
            p++;
            break;
        }

        if (!parse_identifier(&p, property, sizeof(property)) || !parse_char(&p, ':')) {
            fprintf(stderr, "Parse error at line %d: invalid window({ ... }) property\n", line_no);
            return 0;
        }

        if (strcmp(property, "title") == 0) {
            if (!parse_quoted_string(&p, inst.text, sizeof(inst.text))) {
                fprintf(stderr, "Parse error at line %d: title must be a string\n", line_no);
                return 0;
            }
        } else if (strcmp(property, "width") == 0) {
            if (!parse_int(&p, &inst.a)) {
                fprintf(stderr, "Parse error at line %d: width must be an integer\n", line_no);
                return 0;
            }
        } else if (strcmp(property, "height") == 0) {
            if (!parse_int(&p, &inst.b)) {
                fprintf(stderr, "Parse error at line %d: height must be an integer\n", line_no);
                return 0;
            }
        } else if (strcmp(property, "resizable") == 0) {
            if (!parse_bool(&p, &bool_value)) {
                fprintf(stderr, "Parse error at line %d: resizable must be true or false\n", line_no);
                return 0;
            }
            if (bool_value) {
                inst.flags |= WINDOW_FLAG_RESIZABLE;
            } else {
                inst.flags &= ~WINDOW_FLAG_RESIZABLE;
            }
        } else if (strcmp(property, "fullscreen") == 0) {
            if (!parse_bool(&p, &bool_value)) {
                fprintf(stderr, "Parse error at line %d: fullscreen must be true or false\n", line_no);
                return 0;
            }
            if (bool_value) {
                inst.flags |= WINDOW_FLAG_FULLSCREEN;
            } else {
                inst.flags &= ~WINDOW_FLAG_FULLSCREEN;
            }
        } else if (strcmp(property, "icon") == 0) {
            if (!parse_quoted_string(&p, inst.extra, sizeof(inst.extra))) {
                fprintf(stderr, "Parse error at line %d: icon must be a string\n", line_no);
                return 0;
            }
        } else if (strcmp(property, "minWidth") == 0) {
            if (!parse_int(&p, &inst.d)) {
                fprintf(stderr, "Parse error at line %d: minWidth must be an integer\n", line_no);
                return 0;
            }
        } else if (strcmp(property, "minHeight") == 0) {
            if (!parse_int(&p, &inst.e)) {
                fprintf(stderr, "Parse error at line %d: minHeight must be an integer\n", line_no);
                return 0;
            }
        } else if (strcmp(property, "center") == 0) {
            if (!parse_bool(&p, &bool_value)) {
                fprintf(stderr, "Parse error at line %d: center must be true or false\n", line_no);
                return 0;
            }
            if (bool_value) {
                inst.flags |= WINDOW_FLAG_CENTER;
            } else {
                inst.flags &= ~WINDOW_FLAG_CENTER;
            }
        } else {
            fprintf(stderr, "Parse error at line %d: unsupported window property '%s'\n", line_no, property);
            return 0;
        }

        p = skip_spaces(p);
        if (*p == ',') {
            p++;
            continue;
        }
        if (*p == '}') {
            p++;
            break;
        }

        fprintf(stderr, "Parse error at line %d: expected ',' or '}' in window config\n", line_no);
        return 0;
    }

    if (!parse_char(&p, ')') || !parse_statement_end(&p)) {
        fprintf(stderr, "Parse error at line %d: invalid window({ ... }) statement\n", line_no);
        return 0;
    }

    return add_instruction(program, &inst);
}

static int parse_theme_statement(const char* line, int line_no, Program* program) {
    const char* p = line + strlen("theme");
    Instruction inst;

    memset(&inst, 0, sizeof(inst));
    inst.opcode = OP_THEME;
    inst.jump_index = -1;
    inst.style[0] = 30;
    inst.style[1] = 36;
    inst.style[2] = 48;
    inst.style[3] = 45;
    inst.style[4] = 54;
    inst.style[5] = 68;
    inst.style[6] = 107;
    inst.style[7] = 192;
    inst.style[8] = 255;
    inst.style[9] = 240;
    inst.style[10] = 244;
    inst.style[11] = 252;
    inst.style[12] = 153;
    inst.style[13] = 165;
    inst.style[14] = 184;
    inst.style[15] = 24;

    if (!parse_char(&p, '(') || !parse_char(&p, '{')) {
        fprintf(stderr, "Parse error at line %d: expected theme({ ... })\n", line_no);
        return 0;
    }

    while (1) {
        char property[64];

        p = skip_spaces(p);
        if (*p == '}') {
            p++;
            break;
        }

        if (!parse_identifier(&p, property, sizeof(property)) || !parse_char(&p, ':')) {
            fprintf(stderr, "Parse error at line %d: invalid theme({ ... }) property\n", line_no);
            return 0;
        }

        if (strcmp(property, "surfaceR") == 0) {
            if (!parse_int(&p, &inst.style[0])) {
                fprintf(stderr, "Parse error at line %d: theme.surfaceR must be an integer\n", line_no);
                return 0;
            }
        } else if (strcmp(property, "surfaceG") == 0) {
            if (!parse_int(&p, &inst.style[1])) {
                fprintf(stderr, "Parse error at line %d: theme.surfaceG must be an integer\n", line_no);
                return 0;
            }
        } else if (strcmp(property, "surfaceB") == 0) {
            if (!parse_int(&p, &inst.style[2])) {
                fprintf(stderr, "Parse error at line %d: theme.surfaceB must be an integer\n", line_no);
                return 0;
            }
        } else if (strcmp(property, "surface2R") == 0) {
            if (!parse_int(&p, &inst.style[3])) {
                fprintf(stderr, "Parse error at line %d: theme.surface2R must be an integer\n", line_no);
                return 0;
            }
        } else if (strcmp(property, "surface2G") == 0) {
            if (!parse_int(&p, &inst.style[4])) {
                fprintf(stderr, "Parse error at line %d: theme.surface2G must be an integer\n", line_no);
                return 0;
            }
        } else if (strcmp(property, "surface2B") == 0) {
            if (!parse_int(&p, &inst.style[5])) {
                fprintf(stderr, "Parse error at line %d: theme.surface2B must be an integer\n", line_no);
                return 0;
            }
        } else if (strcmp(property, "accentR") == 0) {
            if (!parse_int(&p, &inst.style[6])) {
                fprintf(stderr, "Parse error at line %d: theme.accentR must be an integer\n", line_no);
                return 0;
            }
        } else if (strcmp(property, "accentG") == 0) {
            if (!parse_int(&p, &inst.style[7])) {
                fprintf(stderr, "Parse error at line %d: theme.accentG must be an integer\n", line_no);
                return 0;
            }
        } else if (strcmp(property, "accentB") == 0) {
            if (!parse_int(&p, &inst.style[8])) {
                fprintf(stderr, "Parse error at line %d: theme.accentB must be an integer\n", line_no);
                return 0;
            }
        } else if (strcmp(property, "textR") == 0) {
            if (!parse_int(&p, &inst.style[9])) {
                fprintf(stderr, "Parse error at line %d: theme.textR must be an integer\n", line_no);
                return 0;
            }
        } else if (strcmp(property, "textG") == 0) {
            if (!parse_int(&p, &inst.style[10])) {
                fprintf(stderr, "Parse error at line %d: theme.textG must be an integer\n", line_no);
                return 0;
            }
        } else if (strcmp(property, "textB") == 0) {
            if (!parse_int(&p, &inst.style[11])) {
                fprintf(stderr, "Parse error at line %d: theme.textB must be an integer\n", line_no);
                return 0;
            }
        } else if (strcmp(property, "mutedR") == 0) {
            if (!parse_int(&p, &inst.style[12])) {
                fprintf(stderr, "Parse error at line %d: theme.mutedR must be an integer\n", line_no);
                return 0;
            }
        } else if (strcmp(property, "mutedG") == 0) {
            if (!parse_int(&p, &inst.style[13])) {
                fprintf(stderr, "Parse error at line %d: theme.mutedG must be an integer\n", line_no);
                return 0;
            }
        } else if (strcmp(property, "mutedB") == 0) {
            if (!parse_int(&p, &inst.style[14])) {
                fprintf(stderr, "Parse error at line %d: theme.mutedB must be an integer\n", line_no);
                return 0;
            }
        } else if (strcmp(property, "radius") == 0) {
            if (!parse_int(&p, &inst.style[15])) {
                fprintf(stderr, "Parse error at line %d: theme.radius must be an integer\n", line_no);
                return 0;
            }
        } else {
            fprintf(stderr, "Parse error at line %d: unsupported theme property '%s'\n", line_no, property);
            return 0;
        }

        p = skip_spaces(p);
        if (*p == ',') {
            p++;
            continue;
        }
        if (*p == '}') {
            p++;
            break;
        }

        fprintf(stderr, "Parse error at line %d: expected ',' or '}' in theme config\n", line_no);
        return 0;
    }

    if (!parse_char(&p, ')') || !parse_statement_end(&p)) {
        fprintf(stderr, "Parse error at line %d: invalid theme({ ... }) statement\n", line_no);
        return 0;
    }

    return add_instruction(program, &inst);
}

static int parse_panel_statement(const char* line, int line_no, Program* program) {
    const char* p = line + strlen("panel");
    Instruction inst;
    int has_x = 0;
    int has_y = 0;
    int has_width = 0;
    int has_height = 0;

    memset(&inst, 0, sizeof(inst));
    inst.opcode = OP_PANEL;
    inst.jump_index = -1;
    inst.style[0] = THEME_TONE_SURFACE;
    inst.style[1] = THEME_TONE_SURFACE_2;
    inst.style[2] = 24;

    if (!parse_char(&p, '(') || !parse_char(&p, '{')) {
        fprintf(stderr, "Parse error at line %d: expected panel({ x, y, width, height, tone?, outlineTone?, radius? })\n", line_no);
        return 0;
    }

    while (1) {
        char property[64];

        p = skip_spaces(p);
        if (*p == '}') {
            p++;
            break;
        }

        if (!parse_identifier(&p, property, sizeof(property)) || !parse_char(&p, ':')) {
            fprintf(stderr, "Parse error at line %d: invalid panel({ ... }) property\n", line_no);
            return 0;
        }

        if (strcmp(property, "x") == 0) {
            if (!parse_int(&p, &inst.a)) {
                fprintf(stderr, "Parse error at line %d: panel.x must be an integer\n", line_no);
                return 0;
            }
            has_x = 1;
        } else if (strcmp(property, "y") == 0) {
            if (!parse_int(&p, &inst.b)) {
                fprintf(stderr, "Parse error at line %d: panel.y must be an integer\n", line_no);
                return 0;
            }
            has_y = 1;
        } else if (strcmp(property, "width") == 0) {
            if (!parse_int(&p, &inst.c)) {
                fprintf(stderr, "Parse error at line %d: panel.width must be an integer\n", line_no);
                return 0;
            }
            has_width = 1;
        } else if (strcmp(property, "height") == 0) {
            if (!parse_int(&p, &inst.d)) {
                fprintf(stderr, "Parse error at line %d: panel.height must be an integer\n", line_no);
                return 0;
            }
            has_height = 1;
        } else if (strcmp(property, "radius") == 0) {
            if (!parse_int(&p, &inst.style[2])) {
                fprintf(stderr, "Parse error at line %d: panel.radius must be an integer\n", line_no);
                return 0;
            }
        } else if (strcmp(property, "tone") == 0) {
            char tone[32];
            int tone_value;

            if (!parse_quoted_string(&p, tone, sizeof(tone))) {
                fprintf(stderr, "Parse error at line %d: panel.tone must be a string\n", line_no);
                return 0;
            }

            tone_value = parse_tone_string(tone);
            if (tone_value < 0 || tone_value == THEME_TONE_TEXT || tone_value == THEME_TONE_MUTED) {
                fprintf(stderr, "Parse error at line %d: panel.tone supports surface|surface2|accent\n", line_no);
                return 0;
            }

            inst.style[0] = tone_value;
        } else if (strcmp(property, "outlineTone") == 0) {
            char tone[32];
            int tone_value;

            if (!parse_quoted_string(&p, tone, sizeof(tone))) {
                fprintf(stderr, "Parse error at line %d: panel.outlineTone must be a string\n", line_no);
                return 0;
            }

            tone_value = parse_tone_string(tone);
            if (tone_value < -1 || tone_value == THEME_TONE_TEXT || tone_value == THEME_TONE_MUTED) {
                fprintf(stderr, "Parse error at line %d: panel.outlineTone supports surface|surface2|accent|none\n", line_no);
                return 0;
            }

            inst.style[1] = tone_value;
        } else {
            fprintf(stderr, "Parse error at line %d: unsupported panel property '%s'\n", line_no, property);
            return 0;
        }

        p = skip_spaces(p);
        if (*p == ',') {
            p++;
            continue;
        }
        if (*p == '}') {
            p++;
            break;
        }

        fprintf(stderr, "Parse error at line %d: expected ',' or '}' in panel config\n", line_no);
        return 0;
    }

    if (!parse_char(&p, ')') || !parse_statement_end(&p)) {
        fprintf(stderr, "Parse error at line %d: invalid panel({ ... }) statement\n", line_no);
        return 0;
    }

    if (!has_x || !has_y || !has_width || !has_height) {
        fprintf(stderr, "Parse error at line %d: panel({ ... }) requires x, y, width, height\n", line_no);
        return 0;
    }

    if (inst.c <= 0 || inst.d <= 0) {
        fprintf(stderr, "Parse error at line %d: panel width/height must be > 0\n", line_no);
        return 0;
    }

    return add_instruction(program, &inst);
}

static int parse_label_statement(const char* line, int line_no, Program* program) {
    const char* p = line + strlen("label");
    Instruction inst;
    int has_content = 0;

    memset(&inst, 0, sizeof(inst));
    inst.opcode = OP_LABEL;
    inst.a = 20;
    inst.b = 20;
    inst.jump_index = -1;
    inst.style[0] = 16;
    inst.style[1] = THEME_TONE_TEXT;
    inst.style[2] = 0;

    if (!parse_char(&p, '(') || !parse_char(&p, '{')) {
        fprintf(stderr, "Parse error at line %d: expected label({ x?, y?, content, size?, tone?, bold? })\n", line_no);
        return 0;
    }

    while (1) {
        char property[64];

        p = skip_spaces(p);
        if (*p == '}') {
            p++;
            break;
        }

        if (!parse_identifier(&p, property, sizeof(property)) || !parse_char(&p, ':')) {
            fprintf(stderr, "Parse error at line %d: invalid label({ ... }) property\n", line_no);
            return 0;
        }

        if (strcmp(property, "x") == 0) {
            if (!parse_int(&p, &inst.a)) {
                fprintf(stderr, "Parse error at line %d: label.x must be an integer\n", line_no);
                return 0;
            }
        } else if (strcmp(property, "y") == 0) {
            if (!parse_int(&p, &inst.b)) {
                fprintf(stderr, "Parse error at line %d: label.y must be an integer\n", line_no);
                return 0;
            }
        } else if (strcmp(property, "size") == 0) {
            if (!parse_int(&p, &inst.style[0])) {
                fprintf(stderr, "Parse error at line %d: label.size must be an integer\n", line_no);
                return 0;
            }
        } else if (strcmp(property, "content") == 0) {
            if (!parse_quoted_string(&p, inst.text, sizeof(inst.text))) {
                fprintf(stderr, "Parse error at line %d: label.content must be a string\n", line_no);
                return 0;
            }
            has_content = 1;
        } else if (strcmp(property, "tone") == 0) {
            char tone[32];
            int tone_value;

            if (!parse_quoted_string(&p, tone, sizeof(tone))) {
                fprintf(stderr, "Parse error at line %d: label.tone must be a string\n", line_no);
                return 0;
            }

            tone_value = parse_tone_string(tone);
            if (tone_value == -999 || tone_value == THEME_TONE_NONE) {
                fprintf(stderr, "Parse error at line %d: label.tone supports surface|surface2|accent|text|muted\n", line_no);
                return 0;
            }

            inst.style[1] = tone_value;
        } else if (strcmp(property, "bold") == 0) {
            if (!parse_bool(&p, &inst.style[2])) {
                fprintf(stderr, "Parse error at line %d: label.bold must be true or false\n", line_no);
                return 0;
            }
        } else {
            fprintf(stderr, "Parse error at line %d: unsupported label property '%s'\n", line_no, property);
            return 0;
        }

        p = skip_spaces(p);
        if (*p == ',') {
            p++;
            continue;
        }
        if (*p == '}') {
            p++;
            break;
        }

        fprintf(stderr, "Parse error at line %d: expected ',' or '}' in label config\n", line_no);
        return 0;
    }

    if (!parse_char(&p, ')') || !parse_statement_end(&p)) {
        fprintf(stderr, "Parse error at line %d: invalid label({ ... }) statement\n", line_no);
        return 0;
    }

    if (!has_content) {
        fprintf(stderr, "Parse error at line %d: label({ ... }) requires content\n", line_no);
        return 0;
    }

    return add_instruction(program, &inst);
}

static int parse_media_player_statement(const char* line, int line_no, Program* program) {
    const char* p = line + strlen("media_player");
    Instruction inst;
    int has_x = 0;
    int has_y = 0;
    int has_width = 0;
    int has_height = 0;
    int has_title = 0;
    int has_cover = 0;
    int has_src = 0;

    memset(&inst, 0, sizeof(inst));
    inst.opcode = OP_MEDIA_PLAYER;
    inst.jump_index = -1;
    inst.c = 924;
    inst.d = 532;

    if (!parse_char(&p, '(') || !parse_char(&p, '{')) {
        fprintf(stderr, "Parse error at line %d: expected media_player({ x, y, width, height, title, cover, src })\n", line_no);
        return 0;
    }

    while (1) {
        char property[64];

        p = skip_spaces(p);
        if (*p == '}') {
            p++;
            break;
        }

        if (!parse_identifier(&p, property, sizeof(property)) || !parse_char(&p, ':')) {
            fprintf(stderr, "Parse error at line %d: invalid media_player({ ... }) property\n", line_no);
            return 0;
        }

        if (strcmp(property, "x") == 0) {
            if (!parse_int(&p, &inst.a)) {
                fprintf(stderr, "Parse error at line %d: media_player.x must be an integer\n", line_no);
                return 0;
            }
            has_x = 1;
        } else if (strcmp(property, "y") == 0) {
            if (!parse_int(&p, &inst.b)) {
                fprintf(stderr, "Parse error at line %d: media_player.y must be an integer\n", line_no);
                return 0;
            }
            has_y = 1;
        } else if (strcmp(property, "width") == 0) {
            if (!parse_int(&p, &inst.c)) {
                fprintf(stderr, "Parse error at line %d: media_player.width must be an integer\n", line_no);
                return 0;
            }
            has_width = 1;
        } else if (strcmp(property, "height") == 0) {
            if (!parse_int(&p, &inst.d)) {
                fprintf(stderr, "Parse error at line %d: media_player.height must be an integer\n", line_no);
                return 0;
            }
            has_height = 1;
        } else if (strcmp(property, "title") == 0) {
            if (!parse_quoted_string(&p, inst.text, sizeof(inst.text))) {
                fprintf(stderr, "Parse error at line %d: media_player.title must be a string\n", line_no);
                return 0;
            }
            has_title = 1;
        } else if (strcmp(property, "src") == 0) {
            if (!parse_quoted_string(&p, inst.extra, sizeof(inst.extra))) {
                fprintf(stderr, "Parse error at line %d: media_player.src must be a string\n", line_no);
                return 0;
            }
            has_src = 1;
        } else if (strcmp(property, "cover") == 0) {
            if (!parse_quoted_string(&p, inst.meta, sizeof(inst.meta))) {
                fprintf(stderr, "Parse error at line %d: media_player.cover must be a string\n", line_no);
                return 0;
            }
            has_cover = 1;
        } else {
            fprintf(stderr, "Parse error at line %d: unsupported media_player property '%s'\n", line_no, property);
            return 0;
        }

        p = skip_spaces(p);
        if (*p == ',') {
            p++;
            continue;
        }
        if (*p == '}') {
            p++;
            break;
        }

        fprintf(stderr, "Parse error at line %d: expected ',' or '}' in media_player config\n", line_no);
        return 0;
    }

    if (!parse_char(&p, ')') || !parse_statement_end(&p)) {
        fprintf(stderr, "Parse error at line %d: invalid media_player({ ... }) statement\n", line_no);
        return 0;
    }

    if (!has_x || !has_y || !has_width || !has_height || !has_title || !has_cover || !has_src) {
        fprintf(stderr, "Parse error at line %d: media_player({ ... }) requires x, y, width, height, title, cover, src\n", line_no);
        return 0;
    }

    if (inst.c <= 0 || inst.d <= 0) {
        fprintf(stderr, "Parse error at line %d: media_player width/height must be > 0\n", line_no);
        return 0;
    }

    return add_instruction(program, &inst);
}

static int parse_text_statement(const char* line, int line_no, Program* program) {
    const char* p = line + strlen("text");
    Instruction inst;
    int has_content = 0;

    memset(&inst, 0, sizeof(inst));
    inst.opcode = OP_TEXT;
    inst.a = 20;
    inst.b = 20;
    inst.jump_index = -1;

    if (!parse_char(&p, '(')) {
        fprintf(stderr, "Parse error at line %d: invalid text statement\n", line_no);
        return 0;
    }

    p = skip_spaces(p);
    if (*p == '{') {
        if (!parse_char(&p, '{')) {
            fprintf(stderr, "Parse error at line %d: invalid text({ ... }) statement\n", line_no);
            return 0;
        }

        while (1) {
            char property[64];

            p = skip_spaces(p);
            if (*p == '}') {
                p++;
                break;
            }

            if (!parse_identifier(&p, property, sizeof(property)) || !parse_char(&p, ':')) {
                fprintf(stderr, "Parse error at line %d: invalid text({ ... }) property\n", line_no);
                return 0;
            }

            if (strcmp(property, "x") == 0) {
                if (!parse_int(&p, &inst.a)) {
                    fprintf(stderr, "Parse error at line %d: text.x must be an integer\n", line_no);
                    return 0;
                }
            } else if (strcmp(property, "y") == 0) {
                if (!parse_int(&p, &inst.b)) {
                    fprintf(stderr, "Parse error at line %d: text.y must be an integer\n", line_no);
                    return 0;
                }
            } else if (strcmp(property, "w") == 0 || strcmp(property, "width") == 0) {
                if (!parse_int(&p, &inst.c)) {
                    fprintf(stderr, "Parse error at line %d: text.w must be an integer\n", line_no);
                    return 0;
                }
            } else if (strcmp(property, "h") == 0 || strcmp(property, "height") == 0) {
                if (!parse_int(&p, &inst.d)) {
                    fprintf(stderr, "Parse error at line %d: text.h must be an integer\n", line_no);
                    return 0;
                }
            } else if (strcmp(property, "content") == 0) {
                if (!parse_quoted_string(&p, inst.text, sizeof(inst.text))) {
                    fprintf(stderr, "Parse error at line %d: text.content must be a string\n", line_no);
                    return 0;
                }
                has_content = 1;
            } else {
                fprintf(stderr, "Parse error at line %d: unsupported text property '%s'\n", line_no, property);
                return 0;
            }

            p = skip_spaces(p);
            if (*p == ',') {
                p++;
                continue;
            }
            if (*p == '}') {
                p++;
                break;
            }

            fprintf(stderr, "Parse error at line %d: expected ',' or '}' in text config\n", line_no);
            return 0;
        }

        if (!parse_char(&p, ')') || !parse_statement_end(&p)) {
            fprintf(stderr, "Parse error at line %d: invalid text({ ... }) statement\n", line_no);
            return 0;
        }

        if (!has_content) {
            fprintf(stderr, "Parse error at line %d: text({ ... }) requires content\n", line_no);
            return 0;
        }
    } else {
        if (!parse_int(&p, &inst.a) || !parse_char(&p, ',') || !parse_int(&p, &inst.b) ||
            !parse_char(&p, ',') || !parse_quoted_string(&p, inst.text, sizeof(inst.text)) ||
            !parse_char(&p, ')') || !parse_statement_end(&p)) {
            fprintf(stderr, "Parse error at line %d: expected text(x, y, \"msg\") or text({ x, y, content })\n", line_no);
            return 0;
        }
        inst.flags |= FLAG_LEGACY_CALL;
    }

    return add_instruction(program, &inst);
}

static int parse_print_statement(const char* line, int line_no, Program* program) {
    const char* p = line + strlen("print");
    Instruction inst;

    memset(&inst, 0, sizeof(inst));
    inst.opcode = OP_PRINT;
    inst.jump_index = -1;

    if (!parse_char(&p, '(') ||
        !parse_quoted_string(&p, inst.text, sizeof(inst.text)) ||
        !parse_char(&p, ')') ||
        !parse_statement_end(&p)) {
        fprintf(stderr, "Parse error at line %d: expected print(\"message\")\n", line_no);
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
    strcpy(inst.extra, "HOSC");
    inst.flags = MSG_ICON_INFO | FLAG_LEGACY_CALL;

    if (!parse_char(&p, '(') || !parse_quoted_string(&p, inst.text, sizeof(inst.text)) ||
        !parse_char(&p, ')') || !parse_statement_end(&p)) {
        fprintf(stderr, "Parse error at line %d: invalid win32_message_box(\"msg\") statement\n", line_no);
        return 0;
    }

    return add_instruction(program, &inst);
}

static int parse_rect_statement(const char* line, int line_no, Program* program) {
    const char* p = line + strlen("rect");
    Instruction inst;
    int has_x = 0;
    int has_y = 0;
    int has_width = 0;
    int has_height = 0;
    int bool_value = 0;
    int red = 0;
    int green = 0;
    int blue = 0;

    memset(&inst, 0, sizeof(inst));
    inst.opcode = OP_RECT;
    inst.jump_index = -1;
    inst.text[0] = 1;
    inst.extra[0] = '\0';

    if (!parse_char(&p, '(') || !parse_char(&p, '{')) {
        fprintf(stderr, "Parse error at line %d: expected rect({ x, y, width, height, r, g, b, filled? })\n", line_no);
        return 0;
    }

    while (1) {
        char property[64];

        p = skip_spaces(p);
        if (*p == '}') {
            p++;
            break;
        }

        if (!parse_identifier(&p, property, sizeof(property)) || !parse_char(&p, ':')) {
            fprintf(stderr, "Parse error at line %d: invalid rect({ ... }) property\n", line_no);
            return 0;
        }

        if (strcmp(property, "x") == 0) {
            if (!parse_int(&p, &inst.a)) {
                fprintf(stderr, "Parse error at line %d: rect.x must be an integer\n", line_no);
                return 0;
            }
            has_x = 1;
        } else if (strcmp(property, "y") == 0) {
            if (!parse_int(&p, &inst.b)) {
                fprintf(stderr, "Parse error at line %d: rect.y must be an integer\n", line_no);
                return 0;
            }
            has_y = 1;
        } else if (strcmp(property, "width") == 0) {
            if (!parse_int(&p, &inst.c)) {
                fprintf(stderr, "Parse error at line %d: rect.width must be an integer\n", line_no);
                return 0;
            }
            has_width = 1;
        } else if (strcmp(property, "height") == 0) {
            if (!parse_int(&p, &inst.d)) {
                fprintf(stderr, "Parse error at line %d: rect.height must be an integer\n", line_no);
                return 0;
            }
            has_height = 1;
        } else if (strcmp(property, "r") == 0) {
            if (!parse_int(&p, &red)) {
                fprintf(stderr, "Parse error at line %d: rect.r must be an integer\n", line_no);
                return 0;
            }
        } else if (strcmp(property, "g") == 0) {
            if (!parse_int(&p, &green)) {
                fprintf(stderr, "Parse error at line %d: rect.g must be an integer\n", line_no);
                return 0;
            }
        } else if (strcmp(property, "b") == 0) {
            if (!parse_int(&p, &blue)) {
                fprintf(stderr, "Parse error at line %d: rect.b must be an integer\n", line_no);
                return 0;
            }
        } else if (strcmp(property, "filled") == 0) {
            if (!parse_bool(&p, &bool_value)) {
                fprintf(stderr, "Parse error at line %d: rect.filled must be true or false\n", line_no);
                return 0;
            }
            inst.text[0] = (char)(bool_value ? 1 : 0);
        } else {
            fprintf(stderr, "Parse error at line %d: unsupported rect property '%s'\n", line_no, property);
            return 0;
        }

        p = skip_spaces(p);
        if (*p == ',') {
            p++;
            continue;
        }
        if (*p == '}') {
            p++;
            break;
        }

        fprintf(stderr, "Parse error at line %d: expected ',' or '}' in rect config\n", line_no);
        return 0;
    }

    if (!parse_char(&p, ')') || !parse_statement_end(&p)) {
        fprintf(stderr, "Parse error at line %d: invalid rect({ ... }) statement\n", line_no);
        return 0;
    }

    if (!has_x || !has_y || !has_width || !has_height) {
        fprintf(stderr, "Parse error at line %d: rect({ ... }) requires x, y, width, height\n", line_no);
        return 0;
    }

    if (inst.c <= 0 || inst.d <= 0) {
        fprintf(stderr, "Parse error at line %d: rect width/height must be > 0\n", line_no);
        return 0;
    }

    inst.flags = (unsigned int)red;
    inst.jump_index = green;
    inst.e = blue;

    return add_instruction(program, &inst);
}

static int parse_button_statement(const char* line, int line_no, Program* program) {
    const char* p = line + strlen("button");
    Instruction inst;
    int has_x = 0;
    int has_y = 0;
    int has_width = 0;
    int has_height = 0;
    int has_label = 0;
    int has_action = 0;

    memset(&inst, 0, sizeof(inst));
    inst.opcode = OP_BUTTON;
    inst.jump_index = -1;
    strcpy(inst.extra, "play");

    if (!parse_char(&p, '(') || !parse_char(&p, '{')) {
        fprintf(stderr, "Parse error at line %d: expected button({ x, y, width, height, label, action, src? })\n", line_no);
        return 0;
    }

    while (1) {
        char property[64];

        p = skip_spaces(p);
        if (*p == '}') {
            p++;
            break;
        }

        if (!parse_identifier(&p, property, sizeof(property)) || !parse_char(&p, ':')) {
            fprintf(stderr, "Parse error at line %d: invalid button({ ... }) property\n", line_no);
            return 0;
        }

        if (strcmp(property, "x") == 0) {
            if (!parse_int(&p, &inst.a)) {
                fprintf(stderr, "Parse error at line %d: button.x must be an integer\n", line_no);
                return 0;
            }
            has_x = 1;
        } else if (strcmp(property, "y") == 0) {
            if (!parse_int(&p, &inst.b)) {
                fprintf(stderr, "Parse error at line %d: button.y must be an integer\n", line_no);
                return 0;
            }
            has_y = 1;
        } else if (strcmp(property, "width") == 0) {
            if (!parse_int(&p, &inst.c)) {
                fprintf(stderr, "Parse error at line %d: button.width must be an integer\n", line_no);
                return 0;
            }
            has_width = 1;
        } else if (strcmp(property, "height") == 0) {
            if (!parse_int(&p, &inst.d)) {
                fprintf(stderr, "Parse error at line %d: button.height must be an integer\n", line_no);
                return 0;
            }
            has_height = 1;
        } else if (strcmp(property, "label") == 0) {
            if (!parse_quoted_string(&p, inst.text, sizeof(inst.text))) {
                fprintf(stderr, "Parse error at line %d: button.label must be a string\n", line_no);
                return 0;
            }
            has_label = 1;
        } else if (strcmp(property, "action") == 0) {
            if (!parse_quoted_string(&p, inst.extra, sizeof(inst.extra))) {
                fprintf(stderr, "Parse error at line %d: button.action must be a string\n", line_no);
                return 0;
            }
            has_action = 1;
        } else if (strcmp(property, "src") == 0) {
            if (!parse_quoted_string(&p, inst.extra + 64, sizeof(inst.extra) - 64)) {
                fprintf(stderr, "Parse error at line %d: button.src must be a string\n", line_no);
                return 0;
            }
        } else {
            fprintf(stderr, "Parse error at line %d: unsupported button property '%s'\n", line_no, property);
            return 0;
        }

        p = skip_spaces(p);
        if (*p == ',') {
            p++;
            continue;
        }
        if (*p == '}') {
            p++;
            break;
        }

        fprintf(stderr, "Parse error at line %d: expected ',' or '}' in button config\n", line_no);
        return 0;
    }

    if (!parse_char(&p, ')') || !parse_statement_end(&p)) {
        fprintf(stderr, "Parse error at line %d: invalid button({ ... }) statement\n", line_no);
        return 0;
    }

    if (!has_x || !has_y || !has_width || !has_height || !has_label || !has_action) {
        fprintf(stderr, "Parse error at line %d: button({ ... }) requires x, y, width, height, label, action\n", line_no);
        return 0;
    }

    if (strcmp(inst.extra, "play") != 0 &&
        strcmp(inst.extra, "stop") != 0 &&
        strcmp(inst.extra, "pick_audio") != 0) {
        fprintf(stderr, "Parse error at line %d: button.action supports play|stop|pick_audio\n", line_no);
        return 0;
    }

    if (inst.c <= 0 || inst.d <= 0) {
        fprintf(stderr, "Parse error at line %d: button width/height must be > 0\n", line_no);
        return 0;
    }

    return add_instruction(program, &inst);
}

static int parse_seekbar_statement(const char* line, int line_no, Program* program) {
    const char* p = line + strlen("seekbar");
    Instruction inst;
    int has_x = 0;
    int has_y = 0;
    int has_width = 0;
    int has_height = 0;

    memset(&inst, 0, sizeof(inst));
    inst.opcode = OP_SEEKBAR;
    inst.jump_index = -1;

    if (!parse_char(&p, '(') || !parse_char(&p, '{')) {
        fprintf(stderr, "Parse error at line %d: expected seekbar({ x, y, width, height })\n", line_no);
        return 0;
    }

    while (1) {
        char property[64];

        p = skip_spaces(p);
        if (*p == '}') {
            p++;
            break;
        }

        if (!parse_identifier(&p, property, sizeof(property)) || !parse_char(&p, ':')) {
            fprintf(stderr, "Parse error at line %d: invalid seekbar({ ... }) property\n", line_no);
            return 0;
        }

        if (strcmp(property, "x") == 0) {
            if (!parse_int(&p, &inst.a)) {
                fprintf(stderr, "Parse error at line %d: seekbar.x must be an integer\n", line_no);
                return 0;
            }
            has_x = 1;
        } else if (strcmp(property, "y") == 0) {
            if (!parse_int(&p, &inst.b)) {
                fprintf(stderr, "Parse error at line %d: seekbar.y must be an integer\n", line_no);
                return 0;
            }
            has_y = 1;
        } else if (strcmp(property, "width") == 0) {
            if (!parse_int(&p, &inst.c)) {
                fprintf(stderr, "Parse error at line %d: seekbar.width must be an integer\n", line_no);
                return 0;
            }
            has_width = 1;
        } else if (strcmp(property, "height") == 0) {
            if (!parse_int(&p, &inst.d)) {
                fprintf(stderr, "Parse error at line %d: seekbar.height must be an integer\n", line_no);
                return 0;
            }
            has_height = 1;
        } else {
            fprintf(stderr, "Parse error at line %d: unsupported seekbar property '%s'\n", line_no, property);
            return 0;
        }

        p = skip_spaces(p);
        if (*p == ',') {
            p++;
            continue;
        }
        if (*p == '}') {
            p++;
            break;
        }

        fprintf(stderr, "Parse error at line %d: expected ',' or '}' in seekbar config\n", line_no);
        return 0;
    }

    if (!parse_char(&p, ')') || !parse_statement_end(&p)) {
        fprintf(stderr, "Parse error at line %d: invalid seekbar({ ... }) statement\n", line_no);
        return 0;
    }

    if (!has_x || !has_y || !has_width || !has_height) {
        fprintf(stderr, "Parse error at line %d: seekbar({ ... }) requires x, y, width, height\n", line_no);
        return 0;
    }

    if (inst.c <= 0 || inst.d <= 0) {
        fprintf(stderr, "Parse error at line %d: seekbar width/height must be > 0\n", line_no);
        return 0;
    }

    return add_instruction(program, &inst);
}

static int parse_image_statement(const char* line, int line_no, Program* program) {
    const char* p = line + strlen("image");
    Instruction inst;
    int has_x = 0;
    int has_y = 0;
    int has_width = 0;
    int has_height = 0;
    int has_src = 0;

    memset(&inst, 0, sizeof(inst));
    inst.opcode = OP_IMAGE;
    inst.jump_index = -1;

    if (!parse_char(&p, '(')) {
        fprintf(stderr, "Parse error at line %d: invalid image statement\n", line_no);
        return 0;
    }

    p = skip_spaces(p);
    if (*p == '{') {
        if (!parse_char(&p, '{')) {
            fprintf(stderr, "Parse error at line %d: invalid image({ ... }) statement\n", line_no);
            return 0;
        }

        while (1) {
            char property[64];

            p = skip_spaces(p);
            if (*p == '}') {
                p++;
                break;
            }

            if (!parse_identifier(&p, property, sizeof(property)) || !parse_char(&p, ':')) {
                fprintf(stderr, "Parse error at line %d: invalid image({ ... }) property\n", line_no);
                return 0;
            }

            if (strcmp(property, "x") == 0) {
                if (!parse_int(&p, &inst.a)) {
                    fprintf(stderr, "Parse error at line %d: image.x must be an integer\n", line_no);
                    return 0;
                }
                has_x = 1;
            } else if (strcmp(property, "y") == 0) {
                if (!parse_int(&p, &inst.b)) {
                    fprintf(stderr, "Parse error at line %d: image.y must be an integer\n", line_no);
                    return 0;
                }
                has_y = 1;
            } else if (strcmp(property, "width") == 0) {
                if (!parse_int(&p, &inst.c)) {
                    fprintf(stderr, "Parse error at line %d: image.width must be an integer\n", line_no);
                    return 0;
                }
                has_width = 1;
            } else if (strcmp(property, "height") == 0) {
                if (!parse_int(&p, &inst.d)) {
                    fprintf(stderr, "Parse error at line %d: image.height must be an integer\n", line_no);
                    return 0;
                }
                has_height = 1;
            } else if (strcmp(property, "src") == 0) {
                if (!parse_quoted_string(&p, inst.text, sizeof(inst.text))) {
                    fprintf(stderr, "Parse error at line %d: image.src must be a string\n", line_no);
                    return 0;
                }
                has_src = 1;
            } else {
                fprintf(stderr, "Parse error at line %d: unsupported image property '%s'\n", line_no, property);
                return 0;
            }

            p = skip_spaces(p);
            if (*p == ',') {
                p++;
                continue;
            }
            if (*p == '}') {
                p++;
                break;
            }

            fprintf(stderr, "Parse error at line %d: expected ',' or '}' in image config\n", line_no);
            return 0;
        }

        if (!parse_char(&p, ')') || !parse_statement_end(&p)) {
            fprintf(stderr, "Parse error at line %d: invalid image({ ... }) statement\n", line_no);
            return 0;
        }

        if (!has_x || !has_y || !has_width || !has_height || !has_src) {
            fprintf(stderr, "Parse error at line %d: image({ ... }) requires x, y, width, height, src\n", line_no);
            return 0;
        }
    } else {
        if (!parse_int(&p, &inst.a) || !parse_char(&p, ',') ||
            !parse_int(&p, &inst.b) || !parse_char(&p, ',') ||
            !parse_int(&p, &inst.c) || !parse_char(&p, ',') ||
            !parse_int(&p, &inst.d) || !parse_char(&p, ',') ||
            !parse_quoted_string(&p, inst.text, sizeof(inst.text)) ||
            !parse_char(&p, ')') || !parse_statement_end(&p)) {
            fprintf(stderr, "Parse error at line %d: expected image(x, y, width, height, \"path\") or image({ x, y, width, height, src })\n", line_no);
            return 0;
        }
    }

    if (inst.c <= 0 || inst.d <= 0) {
        fprintf(stderr, "Parse error at line %d: image width/height must be > 0\n", line_no);
        return 0;
    }

    return add_instruction(program, &inst);
}

static int parse_play_sound_statement(const char* line, int line_no, Program* program) {
    const char* p = line + strlen("play_sound");
    Instruction inst;

    memset(&inst, 0, sizeof(inst));
    inst.opcode = OP_PLAY_SOUND;
    inst.jump_index = -1;
    inst.flags = FLAG_LEGACY_CALL;

    if (!parse_char(&p, '(') ||
        !parse_quoted_string(&p, inst.text, sizeof(inst.text)) ||
        !parse_char(&p, ')') || !parse_statement_end(&p)) {
        fprintf(stderr, "Parse error at line %d: invalid play_sound(\"path\") statement\n", line_no);
        return 0;
    }

    return add_instruction(program, &inst);
}

static int parse_audio_play_statement(const char* line, int line_no, Program* program) {
    const char* p = line + strlen("audio.play");
    Instruction inst;
    int has_src = 0;

    memset(&inst, 0, sizeof(inst));
    inst.opcode = OP_PLAY_SOUND;
    inst.jump_index = -1;

    if (!parse_char(&p, '(')) {
        fprintf(stderr, "Parse error at line %d: invalid audio.play statement\n", line_no);
        return 0;
    }

    p = skip_spaces(p);
    if (*p == '{') {
        if (!parse_char(&p, '{')) {
            fprintf(stderr, "Parse error at line %d: invalid audio.play({ ... }) statement\n", line_no);
            return 0;
        }

        while (1) {
            char property[64];

            p = skip_spaces(p);
            if (*p == '}') {
                p++;
                break;
            }

            if (!parse_identifier(&p, property, sizeof(property)) || !parse_char(&p, ':')) {
                fprintf(stderr, "Parse error at line %d: invalid audio.play({ ... }) property\n", line_no);
                return 0;
            }

            if (strcmp(property, "src") == 0) {
                if (!parse_quoted_string(&p, inst.text, sizeof(inst.text))) {
                    fprintf(stderr, "Parse error at line %d: audio.play.src must be a string\n", line_no);
                    return 0;
                }
                has_src = 1;
            } else if (strcmp(property, "loop") == 0) {
                int bool_value = 0;
                if (!parse_bool(&p, &bool_value)) {
                    fprintf(stderr, "Parse error at line %d: audio.play.loop must be true or false\n", line_no);
                    return 0;
                }
                inst.a = bool_value;
            } else if (strcmp(property, "volume") == 0) {
                int ignored_volume = 0;
                if (!parse_int(&p, &ignored_volume)) {
                    fprintf(stderr, "Parse error at line %d: audio.play.volume must be an integer\n", line_no);
                    return 0;
                }
            } else {
                fprintf(stderr, "Parse error at line %d: unsupported audio.play property '%s'\n", line_no, property);
                return 0;
            }

            p = skip_spaces(p);
            if (*p == ',') {
                p++;
                continue;
            }
            if (*p == '}') {
                p++;
                break;
            }

            fprintf(stderr, "Parse error at line %d: expected ',' or '}' in audio.play config\n", line_no);
            return 0;
        }

        if (!parse_char(&p, ')') || !parse_statement_end(&p)) {
            fprintf(stderr, "Parse error at line %d: invalid audio.play({ ... }) statement\n", line_no);
            return 0;
        }

        if (!has_src) {
            fprintf(stderr, "Parse error at line %d: audio.play({ ... }) requires src\n", line_no);
            return 0;
        }
    } else {
        if (!parse_quoted_string(&p, inst.text, sizeof(inst.text)) ||
            !parse_char(&p, ')') || !parse_statement_end(&p)) {
            fprintf(stderr, "Parse error at line %d: expected audio.play(\"path\") or audio.play({ src, ... })\n", line_no);
            return 0;
        }
    }

    return add_instruction(program, &inst);
}

static int parse_audio_stop_statement(const char* line, int line_no, Program* program) {
    const char* p = line + strlen("audio.stop");
    Instruction inst;

    memset(&inst, 0, sizeof(inst));
    inst.opcode = OP_AUDIO_STOP;
    inst.jump_index = -1;

    if (!parse_char(&p, '(') || !parse_char(&p, ')') || !parse_statement_end(&p)) {
        fprintf(stderr, "Parse error at line %d: expected audio.stop()\n", line_no);
        return 0;
    }

    return add_instruction(program, &inst);
}

static int parse_system_message_box_statement(const char* line, int line_no, Program* program) {
    const char* p = line + strlen("system.messageBox");
    Instruction inst;
    int has_content = 0;

    memset(&inst, 0, sizeof(inst));
    inst.opcode = OP_WIN32_MESSAGE_BOX;
    inst.jump_index = -1;
    strcpy(inst.extra, "HOSC");
    inst.flags = MSG_ICON_INFO;

    if (!parse_char(&p, '(')) {
        fprintf(stderr, "Parse error at line %d: invalid system.messageBox statement\n", line_no);
        return 0;
    }

    p = skip_spaces(p);
    if (*p == '{') {
        if (!parse_char(&p, '{')) {
            fprintf(stderr, "Parse error at line %d: invalid system.messageBox({ ... }) statement\n", line_no);
            return 0;
        }

        while (1) {
            char property[64];

            p = skip_spaces(p);
            if (*p == '}') {
                p++;
                break;
            }

            if (!parse_identifier(&p, property, sizeof(property)) || !parse_char(&p, ':')) {
                fprintf(stderr, "Parse error at line %d: invalid system.messageBox({ ... }) property\n", line_no);
                return 0;
            }

            if (strcmp(property, "content") == 0) {
                if (!parse_quoted_string(&p, inst.text, sizeof(inst.text))) {
                    fprintf(stderr, "Parse error at line %d: system.messageBox.content must be a string\n", line_no);
                    return 0;
                }
                has_content = 1;
            } else if (strcmp(property, "title") == 0) {
                if (!parse_quoted_string(&p, inst.extra, sizeof(inst.extra))) {
                    fprintf(stderr, "Parse error at line %d: system.messageBox.title must be a string\n", line_no);
                    return 0;
                }
            } else if (strcmp(property, "icon") == 0) {
                char icon_name[64];
                if (!parse_quoted_string(&p, icon_name, sizeof(icon_name))) {
                    fprintf(stderr, "Parse error at line %d: system.messageBox.icon must be a string\n", line_no);
                    return 0;
                }
                inst.flags &= ~(MSG_ICON_INFO | MSG_ICON_WARNING | MSG_ICON_ERROR);
                if (strcmp(icon_name, "info") == 0) {
                    inst.flags |= MSG_ICON_INFO;
                } else if (strcmp(icon_name, "warning") == 0) {
                    inst.flags |= MSG_ICON_WARNING;
                } else if (strcmp(icon_name, "error") == 0) {
                    inst.flags |= MSG_ICON_ERROR;
                } else if (strcmp(icon_name, "none") == 0) {
                } else {
                    fprintf(stderr, "Parse error at line %d: system.messageBox.icon supports info|warning|error|none\n", line_no);
                    return 0;
                }
            } else {
                fprintf(stderr, "Parse error at line %d: unsupported system.messageBox property '%s'\n", line_no, property);
                return 0;
            }

            p = skip_spaces(p);
            if (*p == ',') {
                p++;
                continue;
            }
            if (*p == '}') {
                p++;
                break;
            }

            fprintf(stderr, "Parse error at line %d: expected ',' or '}' in system.messageBox config\n", line_no);
            return 0;
        }

        if (!parse_char(&p, ')') || !parse_statement_end(&p)) {
            fprintf(stderr, "Parse error at line %d: invalid system.messageBox({ ... }) statement\n", line_no);
            return 0;
        }

        if (!has_content) {
            fprintf(stderr, "Parse error at line %d: system.messageBox({ ... }) requires content\n", line_no);
            return 0;
        }
    } else {
        if (!parse_quoted_string(&p, inst.text, sizeof(inst.text)) ||
            !parse_char(&p, ')') || !parse_statement_end(&p)) {
            fprintf(stderr, "Parse error at line %d: expected system.messageBox(\"text\") or system.messageBox({ ... })\n", line_no);
            return 0;
        }
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
    int in_block_comment = 0;

    file = fopen(path, "r");
    if (!file) {
        fprintf(stderr, "Cannot open script: %s\n", path);
        return 0;
    }

    program->count = 0;

    while (fgets(line, sizeof(line), file)) {
        char* p = line;
        char statement[MAX_WINDOW_STATEMENT];
        const char* loop_paren = NULL;
        const char* loop_close = NULL;
        const char* loop_brace = NULL;

        line_no++;

        while (1) {
            while (*p && isspace((unsigned char)*p)) {
                p++;
            }

            if (in_block_comment) {
                char* block_end = strstr(p, "*/");
                if (!block_end) {
                    p = NULL;
                    break;
                }
                in_block_comment = 0;
                p = block_end + 2;
                continue;
            }

            if (p[0] == '/' && p[1] == '*') {
                in_block_comment = 1;
                p += 2;
                continue;
            }

            break;
        }

        if (!p || *p == '\0' || *p == '#' || (p[0] == '/' && p[1] == '/')) {
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
            size_t stmt_len = strlen(p);
            if (stmt_len >= sizeof(statement)) {
                fprintf(stderr, "Parse error at line %d: window statement too long\n", line_no);
                fclose(file);
                return 0;
            }
            memcpy(statement, p, stmt_len + 1);
            if (!read_window_statement(file, statement, sizeof(statement), &line_no)) {
                fprintf(stderr, "Parse error at line %d: unterminated window statement\n", line_no);
                fclose(file);
                return 0;
            }
            if (!parse_window_statement(statement, line_no, program)) {
                fclose(file);
                return 0;
            }
            continue;
        }

        if (starts_with(p, "text")) {
            size_t stmt_len = strlen(p);
            if (stmt_len >= sizeof(statement)) {
                fprintf(stderr, "Parse error at line %d: text statement too long\n", line_no);
                fclose(file);
                return 0;
            }
            memcpy(statement, p, stmt_len + 1);
            if (!read_window_statement(file, statement, sizeof(statement), &line_no)) {
                fprintf(stderr, "Parse error at line %d: unterminated text statement\n", line_no);
                fclose(file);
                return 0;
            }
            if (!parse_text_statement(statement, line_no, program)) {
                fclose(file);
                return 0;
            }
            continue;
        }

        if (starts_with(p, "print")) {
            if (!parse_print_statement(p, line_no, program)) {
                fclose(file);
                return 0;
            }
            continue;
        }

        if (starts_with(p, "theme")) {
            size_t stmt_len = strlen(p);
            if (stmt_len >= sizeof(statement)) {
                fprintf(stderr, "Parse error at line %d: theme statement too long\n", line_no);
                fclose(file);
                return 0;
            }
            memcpy(statement, p, stmt_len + 1);
            if (!read_window_statement(file, statement, sizeof(statement), &line_no)) {
                fprintf(stderr, "Parse error at line %d: unterminated theme statement\n", line_no);
                fclose(file);
                return 0;
            }
            if (!parse_theme_statement(statement, line_no, program)) {
                fclose(file);
                return 0;
            }
            continue;
        }

        if (starts_with(p, "panel")) {
            size_t stmt_len = strlen(p);
            if (stmt_len >= sizeof(statement)) {
                fprintf(stderr, "Parse error at line %d: panel statement too long\n", line_no);
                fclose(file);
                return 0;
            }
            memcpy(statement, p, stmt_len + 1);
            if (!read_window_statement(file, statement, sizeof(statement), &line_no)) {
                fprintf(stderr, "Parse error at line %d: unterminated panel statement\n", line_no);
                fclose(file);
                return 0;
            }
            if (!parse_panel_statement(statement, line_no, program)) {
                fclose(file);
                return 0;
            }
            continue;
        }

        if (starts_with(p, "label")) {
            size_t stmt_len = strlen(p);
            if (stmt_len >= sizeof(statement)) {
                fprintf(stderr, "Parse error at line %d: label statement too long\n", line_no);
                fclose(file);
                return 0;
            }
            memcpy(statement, p, stmt_len + 1);
            if (!read_window_statement(file, statement, sizeof(statement), &line_no)) {
                fprintf(stderr, "Parse error at line %d: unterminated label statement\n", line_no);
                fclose(file);
                return 0;
            }
            if (!parse_label_statement(statement, line_no, program)) {
                fclose(file);
                return 0;
            }
            continue;
        }

        if (starts_with(p, "media_player")) {
            size_t stmt_len = strlen(p);
            if (stmt_len >= sizeof(statement)) {
                fprintf(stderr, "Parse error at line %d: media_player statement too long\n", line_no);
                fclose(file);
                return 0;
            }
            memcpy(statement, p, stmt_len + 1);
            if (!read_window_statement(file, statement, sizeof(statement), &line_no)) {
                fprintf(stderr, "Parse error at line %d: unterminated media_player statement\n", line_no);
                fclose(file);
                return 0;
            }
            if (!parse_media_player_statement(statement, line_no, program)) {
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

        if (starts_with(p, "audio.play")) {
            size_t stmt_len = strlen(p);
            if (stmt_len >= sizeof(statement)) {
                fprintf(stderr, "Parse error at line %d: audio.play statement too long\n", line_no);
                fclose(file);
                return 0;
            }
            memcpy(statement, p, stmt_len + 1);
            if (!read_window_statement(file, statement, sizeof(statement), &line_no)) {
                fprintf(stderr, "Parse error at line %d: unterminated audio.play statement\n", line_no);
                fclose(file);
                return 0;
            }
            if (!parse_audio_play_statement(statement, line_no, program)) {
                fclose(file);
                return 0;
            }
            continue;
        }

        if (starts_with(p, "audio.stop")) {
            if (!parse_audio_stop_statement(p, line_no, program)) {
                fclose(file);
                return 0;
            }
            continue;
        }

        if (starts_with(p, "button")) {
            size_t stmt_len = strlen(p);
            if (stmt_len >= sizeof(statement)) {
                fprintf(stderr, "Parse error at line %d: button statement too long\n", line_no);
                fclose(file);
                return 0;
            }
            memcpy(statement, p, stmt_len + 1);
            if (!read_window_statement(file, statement, sizeof(statement), &line_no)) {
                fprintf(stderr, "Parse error at line %d: unterminated button statement\n", line_no);
                fclose(file);
                return 0;
            }
            if (!parse_button_statement(statement, line_no, program)) {
                fclose(file);
                return 0;
            }
            continue;
        }

        if (starts_with(p, "seekbar")) {
            size_t stmt_len = strlen(p);
            if (stmt_len >= sizeof(statement)) {
                fprintf(stderr, "Parse error at line %d: seekbar statement too long\n", line_no);
                fclose(file);
                return 0;
            }
            memcpy(statement, p, stmt_len + 1);
            if (!read_window_statement(file, statement, sizeof(statement), &line_no)) {
                fprintf(stderr, "Parse error at line %d: unterminated seekbar statement\n", line_no);
                fclose(file);
                return 0;
            }
            if (!parse_seekbar_statement(statement, line_no, program)) {
                fclose(file);
                return 0;
            }
            continue;
        }

        if (starts_with(p, "system.messageBox")) {
            size_t stmt_len = strlen(p);
            if (stmt_len >= sizeof(statement)) {
                fprintf(stderr, "Parse error at line %d: system.messageBox statement too long\n", line_no);
                fclose(file);
                return 0;
            }
            memcpy(statement, p, stmt_len + 1);
            if (!read_window_statement(file, statement, sizeof(statement), &line_no)) {
                fprintf(stderr, "Parse error at line %d: unterminated system.messageBox statement\n", line_no);
                fclose(file);
                return 0;
            }
            if (!parse_system_message_box_statement(statement, line_no, program)) {
                fclose(file);
                return 0;
            }
            continue;
        }

        if (starts_with(p, "image")) {
            size_t stmt_len = strlen(p);
            if (stmt_len >= sizeof(statement)) {
                fprintf(stderr, "Parse error at line %d: image statement too long\n", line_no);
                fclose(file);
                return 0;
            }
            memcpy(statement, p, stmt_len + 1);
            if (!read_window_statement(file, statement, sizeof(statement), &line_no)) {
                fprintf(stderr, "Parse error at line %d: unterminated image statement\n", line_no);
                fclose(file);
                return 0;
            }
            if (!parse_image_statement(statement, line_no, program)) {
                fclose(file);
                return 0;
            }
            continue;
        }

        if (starts_with(p, "rect")) {
            size_t stmt_len = strlen(p);
            if (stmt_len >= sizeof(statement)) {
                fprintf(stderr, "Parse error at line %d: rect statement too long\n", line_no);
                fclose(file);
                return 0;
            }
            memcpy(statement, p, stmt_len + 1);
            if (!read_window_statement(file, statement, sizeof(statement), &line_no)) {
                fprintf(stderr, "Parse error at line %d: unterminated rect statement\n", line_no);
                fclose(file);
                return 0;
            }
            if (!parse_rect_statement(statement, line_no, program)) {
                fclose(file);
                return 0;
            }
            continue;
        }

        if (starts_with(p, "play_sound")) {
            if (!parse_play_sound_statement(p, line_no, program)) {
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

static int collect_frame_events(HOSCGUIEvent* events, int max_events, int* quit_requested) {
    int count = 0;
    HOSCGUIEvent event;

    while (count < max_events && hosc_gui_poll_event(&event)) {
        events[count++] = event;
        if (event.type == HOSC_GUI_EVENT_QUIT) {
            *quit_requested = 1;
        }
    }

    return count;
}

static void handle_click_events(const Instruction* inst, const HOSCGUIEvent* events, int count, int* touched_gui) {
    int i;

    for (i = 0; i < count; i++) {
        if (events[i].type == HOSC_GUI_EVENT_MOUSE_DOWN) {
            char message[768];
            snprintf(message, sizeof(message), "%s (button=%d, x=%d, y=%d)",
                     inst->text, events[i].mouse_button, events[i].mouse_x, events[i].mouse_y);
            hosc_gui_draw_text(inst->a, inst->b, message);
            *touched_gui = 1;
        }
    }
}

static void handle_key_events(const Instruction* inst, const HOSCGUIEvent* events, int count, int* touched_gui) {
    int i;

    for (i = 0; i < count; i++) {
        if (events[i].type == HOSC_GUI_EVENT_KEY_DOWN) {
            if (inst->c == -1 || inst->c == events[i].key_code) {
                char message[768];
                snprintf(message, sizeof(message), "%s (key=%d)", inst->text, events[i].key_code);
                hosc_gui_draw_text(inst->a, inst->b, message);
                *touched_gui = 1;
            }
        }
    }
}

static void handle_mouse_move_events(const Instruction* inst, const HOSCGUIEvent* events, int count, int* touched_gui) {
    int i;

    for (i = 0; i < count; i++) {
        if (events[i].type == HOSC_GUI_EVENT_MOUSE_MOVE) {
            char message[768];
            snprintf(message, sizeof(message), "%s (x=%d, y=%d)", inst->text, events[i].mouse_x, events[i].mouse_y);
            hosc_gui_draw_text(inst->a, inst->b, message);
            *touched_gui = 1;
        }
    }
}

static void draw_button(const Instruction* inst) {
    hosc_gui_suspend_present();
    hosc_gui_draw_round_rect(inst->a, inst->b + 4, inst->c, inst->d, g_ui_theme.radius,
                             18, 22, 31, 18, 22, 31, 0);
    hosc_gui_draw_round_rect(inst->a, inst->b, inst->c, inst->d, g_ui_theme.radius,
                             g_ui_theme.surface_2[0], g_ui_theme.surface_2[1], g_ui_theme.surface_2[2],
                             g_ui_theme.accent[0], g_ui_theme.accent[1], g_ui_theme.accent[2], 1);
    hosc_gui_draw_text_styled(inst->a + 18, inst->b + (inst->d / 2) - 11, inst->text,
                              18,
                              g_ui_theme.text[0], g_ui_theme.text[1], g_ui_theme.text[2],
                              true);
    hosc_gui_resume_present();
    hosc_gui_flush();
}

static void format_time_ms(int ms, char* out, size_t out_cap) {
    int total_seconds;
    int minutes;
    int seconds;

    if (!out || out_cap == 0) {
        return;
    }

    if (ms < 0) {
        snprintf(out, out_cap, "--:--");
        return;
    }

    total_seconds = ms / 1000;
    minutes = total_seconds / 60;
    seconds = total_seconds % 60;
    snprintf(out, out_cap, "%02d:%02d", minutes, seconds);
}

static void draw_seekbar(const Instruction* inst) {
    int duration_ms = hosc_audio_get_duration_ms();
    int position_ms = hosc_audio_get_position_ms();
    int progress_width = 0;
    char left_label[32];
    char right_label[32];
    char status_label[128];

    hosc_gui_suspend_present();
    hosc_gui_draw_round_rect(inst->a, inst->b - 28, inst->c, inst->d + 54, g_ui_theme.radius,
                             g_ui_theme.surface[0], g_ui_theme.surface[1], g_ui_theme.surface[2],
                             g_ui_theme.surface_2[0], g_ui_theme.surface_2[1], g_ui_theme.surface_2[2], 1);
    hosc_gui_draw_round_rect(inst->a, inst->b, inst->c, inst->d, inst->d / 2,
                             g_ui_theme.surface_2[0], g_ui_theme.surface_2[1], g_ui_theme.surface_2[2],
                             g_ui_theme.surface_2[0], g_ui_theme.surface_2[1], g_ui_theme.surface_2[2], 0);

    if (duration_ms > 0 && position_ms >= 0) {
        progress_width = (int)(((long long)inst->c * (long long)position_ms) / (long long)duration_ms);
        if (progress_width < 0) {
            progress_width = 0;
        }
        if (progress_width > inst->c) {
            progress_width = inst->c;
        }
    }

    if (progress_width > 0) {
        hosc_gui_draw_round_rect(inst->a, inst->b, progress_width, inst->d, inst->d / 2,
                                 g_ui_theme.accent[0], g_ui_theme.accent[1], g_ui_theme.accent[2],
                                 g_ui_theme.accent[0], g_ui_theme.accent[1], g_ui_theme.accent[2], 0);
    }

    if (duration_ms > 0 && position_ms >= 0) {
        int knob_x = inst->a + progress_width - 4;
        if (knob_x < inst->a) {
            knob_x = inst->a;
        }
        if (knob_x > inst->a + inst->c - 8) {
            knob_x = inst->a + inst->c - 8;
        }
        hosc_gui_draw_round_rect(knob_x, inst->b - 5, 10, inst->d + 10, 5,
                                 g_ui_theme.text[0], g_ui_theme.text[1], g_ui_theme.text[2],
                                 g_ui_theme.text[0], g_ui_theme.text[1], g_ui_theme.text[2], 0);
    }

    format_time_ms(position_ms, left_label, sizeof(left_label));
    format_time_ms(duration_ms, right_label, sizeof(right_label));
    hosc_gui_draw_text_styled(inst->a, inst->b - 22, left_label, 14,
                              g_ui_theme.muted[0], g_ui_theme.muted[1], g_ui_theme.muted[2], false);
    hosc_gui_draw_text_styled(inst->a + inst->c - 44, inst->b - 22, right_label, 14,
                              g_ui_theme.muted[0], g_ui_theme.muted[1], g_ui_theme.muted[2], false);

    if (hosc_audio_has_internal_playback()) {
        snprintf(status_label, sizeof(status_label), "Click timeline to seek");
    } else {
        snprintf(status_label, sizeof(status_label), "Seek unavailable: internal MCI playback is not active");
    }
    hosc_gui_draw_text_styled(inst->a, inst->b + inst->d + 12, status_label, 13,
                              g_ui_theme.muted[0], g_ui_theme.muted[1], g_ui_theme.muted[2], false);
    hosc_gui_resume_present();
    hosc_gui_flush();
}

static void draw_panel(const Instruction* inst) {
    int fill_r;
    int fill_g;
    int fill_b;
    int border_r = 0;
    int border_g = 0;
    int border_b = 0;
    int border_width = 0;

    theme_color(inst->style[0], &fill_r, &fill_g, &fill_b);
    if (inst->style[1] != THEME_TONE_NONE) {
        theme_color(inst->style[1], &border_r, &border_g, &border_b);
        border_width = 1;
    }

    hosc_gui_draw_round_rect(inst->a, inst->b, inst->c, inst->d,
                             (inst->style[2] > 0 ? inst->style[2] : g_ui_theme.radius),
                             fill_r, fill_g, fill_b,
                             border_r, border_g, border_b,
                             border_width);
}

static void draw_label(const Instruction* inst) {
    int r;
    int g;
    int b;

    theme_color(inst->style[1], &r, &g, &b);
    hosc_gui_draw_text_styled(inst->a, inst->b, inst->text,
                              (inst->style[0] > 0 ? inst->style[0] : 16),
                              r, g, b,
                              (inst->style[2] != 0));
}

static const char* file_name_only(const char* path) {
    const char* cursor = path;
    const char* last = path;

    if (!path || !path[0]) {
        return "";
    }

    while (*cursor) {
        if (*cursor == '\\' || *cursor == '/') {
            last = cursor + 1;
        }
        cursor++;
    }

    return last;
}

static void draw_media_player(const Instruction* inst) {
    int x = inst->a;
    int y = inst->b;
    int width = inst->c;
    int height = inst->d;
    int header_h = 82;
    int left_w = 420;
    int top_gap = 102;
    int left_x = x + 28;
    int left_y = y + top_gap;
    int right_x = x + 474;
    int right_y = y + top_gap;
    int right_w = width - 502;
    int lower_y = y + 420;
    int lower_h = height - 444;

    hosc_gui_suspend_present();
    hosc_gui_draw_round_rect(x, y, width, height, 0,
                             g_ui_theme.surface[0], g_ui_theme.surface[1], g_ui_theme.surface[2],
                             g_ui_theme.surface[0], g_ui_theme.surface[1], g_ui_theme.surface[2], 0);
    hosc_gui_draw_round_rect(x + 28, y + 24, width - 56, header_h, 24,
                             g_ui_theme.surface_2[0], g_ui_theme.surface_2[1], g_ui_theme.surface_2[2],
                             g_ui_theme.accent[0], g_ui_theme.accent[1], g_ui_theme.accent[2], 1);
    hosc_gui_draw_round_rect(x + 56, y + 58, 190, 16, 8,
                             g_ui_theme.accent[0], g_ui_theme.accent[1], g_ui_theme.accent[2],
                             g_ui_theme.accent[0], g_ui_theme.accent[1], g_ui_theme.accent[2], 0);
    hosc_gui_draw_round_rect(left_x, left_y, left_w, 430, 30,
                             g_ui_theme.surface_2[0], g_ui_theme.surface_2[1], g_ui_theme.surface_2[2],
                             g_ui_theme.surface[0], g_ui_theme.surface[1], g_ui_theme.surface[2], 1);
    hosc_gui_draw_round_rect(right_x, right_y, right_w, 298, 30,
                             g_ui_theme.surface_2[0], g_ui_theme.surface_2[1], g_ui_theme.surface_2[2],
                             g_ui_theme.surface[0], g_ui_theme.surface[1], g_ui_theme.surface[2], 1);
    hosc_gui_draw_round_rect(right_x, lower_y, right_w, lower_h, 30,
                             g_ui_theme.surface_2[0], g_ui_theme.surface_2[1], g_ui_theme.surface_2[2],
                             g_ui_theme.accent[0], g_ui_theme.accent[1], g_ui_theme.accent[2], 1);
    hosc_gui_draw_round_rect(right_x + 42, right_y + 38, 152, 24, 12,
                             g_ui_theme.accent[0], g_ui_theme.accent[1], g_ui_theme.accent[2],
                             g_ui_theme.accent[0], g_ui_theme.accent[1], g_ui_theme.accent[2], 0);
    hosc_gui_draw_image(left_x + 30, left_y + 30, 360, 360, inst->meta);

    hosc_gui_draw_text_styled(x + 56, y + 42, "HOSC MEDIA CENTER", 30,
                              g_ui_theme.text[0], g_ui_theme.text[1], g_ui_theme.text[2], true);
    hosc_gui_draw_text_styled(x + 56, y + 82, "Now playing from media_player component", 14,
                              g_ui_theme.muted[0], g_ui_theme.muted[1], g_ui_theme.muted[2], false);
    hosc_gui_draw_text_styled(right_x + 42, right_y + 41, "NOW PLAYING", 11,
                              g_ui_theme.surface[0], g_ui_theme.surface[1], g_ui_theme.surface[2], true);
    hosc_gui_draw_text_styled(right_x + 42, right_y + 72, inst->text, 30,
                              g_ui_theme.text[0], g_ui_theme.text[1], g_ui_theme.text[2], true);
    hosc_gui_draw_text_styled(right_x + 42, right_y + 106, file_name_only(inst->extra), 15,
                              g_ui_theme.muted[0], g_ui_theme.muted[1], g_ui_theme.muted[2], false);
    hosc_gui_draw_text_styled(right_x + 42, right_y + 150, "Component controls: OPEN, PLAY, STOP, REPLAY.", 16,
                              g_ui_theme.text[0], g_ui_theme.text[1], g_ui_theme.text[2], false);
    hosc_gui_draw_text_styled(right_x + 42, lower_y + 90,
                              "OPEN lets you choose a local audio file. Click the timeline to seek when internal playback is active.",
                              14,
                              g_ui_theme.muted[0], g_ui_theme.muted[1], g_ui_theme.muted[2], false);

    hosc_gui_resume_present();
    hosc_gui_flush();

    {
        Instruction button = *inst;
        button.opcode = OP_BUTTON;

        button.a = right_x + 42;
        button.b = lower_y + 26;
        button.c = 120;
        button.d = 42;
        strcpy(button.text, "OPEN");
        strcpy(button.extra, "pick_audio");
        draw_button(&button);

        button.a = right_x + 174;
        button.b = lower_y + 26;
        button.c = 84;
        button.d = 42;
        strcpy(button.text, "PLAY");
        strcpy(button.extra, "play");
        strncpy(button.extra + 64, inst->extra, sizeof(button.extra) - 64 - 1);
        button.extra[sizeof(button.extra) - 1] = '\0';
        draw_button(&button);

        button.a = right_x + 270;
        button.b = lower_y + 18;
        button.c = 108;
        button.d = 58;
        strcpy(button.text, "STOP");
        strcpy(button.extra, "stop");
        draw_button(&button);

        button.a = right_x + 394;
        button.b = lower_y + 26;
        button.c = 84;
        button.d = 42;
        strcpy(button.text, "REPLAY");
        strcpy(button.extra, "play");
        strncpy(button.extra + 64, inst->extra, sizeof(button.extra) - 64 - 1);
        button.extra[sizeof(button.extra) - 1] = '\0';
        draw_button(&button);
    }

    {
        Instruction seekbar = *inst;
        seekbar.opcode = OP_SEEKBAR;
        seekbar.a = right_x + 42;
        seekbar.b = right_y + 196;
        seekbar.c = 390;
        seekbar.d = 10;
        draw_seekbar(&seekbar);
    }
}

static void handle_button_clicks(const Program* program, const HOSCGUIEvent* events, int count, int* touched_gui) {
    int i;
    int j;

    for (i = 0; i < count; i++) {
        if (events[i].type != HOSC_GUI_EVENT_MOUSE_DOWN) {
            continue;
        }

        for (j = 0; j < program->count; j++) {
            const Instruction* inst = &program->items[j];
            if (inst->opcode != OP_BUTTON) {
                continue;
            }

            if (events[i].mouse_x >= inst->a &&
                events[i].mouse_x <= inst->a + inst->c &&
                events[i].mouse_y >= inst->b &&
                events[i].mouse_y <= inst->b + inst->d) {
                if (strcmp(inst->extra, "play") == 0) {
                    const char* src = inst->extra + 64;
                    if (!src[0] || !hosc_audio_play_file(src, true)) {
                        fprintf(stderr, "[audio] failed to play from button: %s\n", (src[0] ? src : "<missing src>"));
                    }
                } else if (strcmp(inst->extra, "stop") == 0) {
                    hosc_audio_stop();
                } else if (strcmp(inst->extra, "pick_audio") == 0) {
                    char picked_path[512];
                    if (hosc_gui_pick_audio_file(picked_path, sizeof(picked_path))) {
                        if (!hosc_audio_play_file(picked_path, true)) {
                            fprintf(stderr, "[audio] failed to play selected file: %s\n", picked_path);
                        }
                    }
                }

                *touched_gui = 1;
            }
        }
    }
}

static void handle_seekbar_clicks(const Program* program, const HOSCGUIEvent* events, int count, int* touched_gui) {
    int i;
    int j;

    for (i = 0; i < count; i++) {
        if (events[i].type != HOSC_GUI_EVENT_MOUSE_DOWN) {
            continue;
        }

        for (j = 0; j < program->count; j++) {
            const Instruction* inst = &program->items[j];
            int duration_ms;
            int relative_x;
            int target_ms;

            if (inst->opcode != OP_SEEKBAR) {
                continue;
            }

            if (events[i].mouse_x < inst->a ||
                events[i].mouse_x > inst->a + inst->c ||
                events[i].mouse_y < inst->b - 4 ||
                events[i].mouse_y > inst->b + inst->d + 4) {
                continue;
            }

            duration_ms = hosc_audio_get_duration_ms();
            if (duration_ms <= 0) {
                continue;
            }

            relative_x = events[i].mouse_x - inst->a;
            if (relative_x < 0) {
                relative_x = 0;
            }
            if (relative_x > inst->c) {
                relative_x = inst->c;
            }

            target_ms = (int)(((long long)duration_ms * (long long)relative_x) / (long long)inst->c);
            if (hosc_audio_seek_ms(target_ms)) {
                *touched_gui = 1;
            }
        }
    }
}

static void handle_media_player_clicks(const Program* program, const HOSCGUIEvent* events, int count, int* touched_gui) {
    int i;
    int j;

    for (i = 0; i < count; i++) {
        if (events[i].type != HOSC_GUI_EVENT_MOUSE_DOWN) {
            continue;
        }

        for (j = 0; j < program->count; j++) {
            const Instruction* inst = &program->items[j];
            int right_x;
            int right_y;
            int lower_y;
            int click_x;
            int click_y;

            if (inst->opcode != OP_MEDIA_PLAYER) {
                continue;
            }

            right_x = inst->a + 474;
            right_y = inst->b + 102;
            lower_y = inst->b + 420;
            click_x = events[i].mouse_x;
            click_y = events[i].mouse_y;

            if (click_x >= right_x + 42 && click_x <= right_x + 162 &&
                click_y >= lower_y + 26 && click_y <= lower_y + 68) {
                char picked_path[512];
                if (hosc_gui_pick_audio_file(picked_path, sizeof(picked_path))) {
                    if (!hosc_audio_play_file(picked_path, true)) {
                        fprintf(stderr, "[audio] failed to play selected file: %s\n", picked_path);
                    }
                }
                *touched_gui = 1;
                continue;
            }

            if (click_x >= right_x + 174 && click_x <= right_x + 258 &&
                click_y >= lower_y + 26 && click_y <= lower_y + 68) {
                if (!hosc_audio_play_file(inst->extra, true)) {
                    fprintf(stderr, "[audio] failed to play selected file: %s\n", inst->extra);
                }
                *touched_gui = 1;
                continue;
            }

            if (click_x >= right_x + 270 && click_x <= right_x + 378 &&
                click_y >= lower_y + 18 && click_y <= lower_y + 76) {
                hosc_audio_stop();
                *touched_gui = 1;
                continue;
            }

            if (click_x >= right_x + 394 && click_x <= right_x + 478 &&
                click_y >= lower_y + 26 && click_y <= lower_y + 68) {
                if (!hosc_audio_play_file(inst->extra, true)) {
                    fprintf(stderr, "[audio] failed to play selected file: %s\n", inst->extra);
                }
                *touched_gui = 1;
                continue;
            }

            if (click_x >= right_x + 42 && click_x <= right_x + 432 &&
                click_y >= right_y + 192 && click_y <= right_y + 206) {
                int duration_ms = hosc_audio_get_duration_ms();
                int relative_x = click_x - (right_x + 42);
                int target_ms;

                if (duration_ms <= 0) {
                    continue;
                }

                if (relative_x < 0) {
                    relative_x = 0;
                }
                if (relative_x > 390) {
                    relative_x = 390;
                }

                target_ms = (int)(((long long)duration_ms * (long long)relative_x) / 390LL);
                if (hosc_audio_seek_ms(target_ms)) {
                    *touched_gui = 1;
                }
            }
        }
    }
}

static int execute_program(const Program* program, int* touched_gui) {
    int pc = 0;
    int should_quit = 0;
    int loop_remaining[MAX_INSTRUCTIONS];
    int frame_events_valid = 0;
    int frame_event_count = 0;
    HOSCGUIEvent frame_events[MAX_FRAME_EVENTS];

    memset(loop_remaining, 0xFF, sizeof(loop_remaining));
    reset_ui_theme();

    while (pc < program->count && !should_quit) {
        const Instruction* inst = &program->items[pc];

        if (*touched_gui && hosc_gui_backend() != HOSC_GUI_BACKEND_CONSOLE && !hosc_gui_is_running()) {
            should_quit = 1;
            break;
        }

        switch (inst->opcode) {
            case OP_WINDOW: {
                HOSCGUIWindowOptions options;

                memset(&options, 0, sizeof(options));
                options.title = inst->text;
                options.width = inst->a;
                options.height = inst->b;
                options.resizable = ((inst->flags & WINDOW_FLAG_RESIZABLE) != 0U);
                options.fullscreen = ((inst->flags & WINDOW_FLAG_FULLSCREEN) != 0U);
                options.icon = (inst->extra[0] ? inst->extra : NULL);
                options.min_width = inst->d;
                options.min_height = inst->e;
                options.center = ((inst->flags & WINDOW_FLAG_CENTER) != 0U);

                hosc_gui_create_window_ex(&options);
                *touched_gui = 1;
                frame_events_valid = 0;
                pc++;
                break;
            }
            case OP_THEME:
                memcpy(g_ui_theme.surface, &inst->style[0], sizeof(int) * 3);
                memcpy(g_ui_theme.surface_2, &inst->style[3], sizeof(int) * 3);
                memcpy(g_ui_theme.accent, &inst->style[6], sizeof(int) * 3);
                memcpy(g_ui_theme.text, &inst->style[9], sizeof(int) * 3);
                memcpy(g_ui_theme.muted, &inst->style[12], sizeof(int) * 3);
                g_ui_theme.radius = inst->style[15];
                pc++;
                break;
            case OP_PANEL:
                draw_panel(inst);
                *touched_gui = 1;
                pc++;
                break;
            case OP_LABEL:
                draw_label(inst);
                *touched_gui = 1;
                pc++;
                break;
            case OP_MEDIA_PLAYER:
                draw_media_player(inst);
                *touched_gui = 1;
                pc++;
                break;
            case OP_PRINT:
                printf("%s\n", inst->text);
                pc++;
                break;
            case OP_TEXT:
                if ((inst->flags & FLAG_LEGACY_CALL) != 0U) {
                    static int warned_text_legacy = 0;
                    if (!warned_text_legacy) {
                        fprintf(stderr, "[deprecated] text(x, y, \"...\") is legacy. Prefer text({ x, y, content }).\n");
                        warned_text_legacy = 1;
                    }
                }
                if (inst->c > 0 && inst->d > 0 && inst->text[0] != '\0') {
                    /* w/h directly control the FONT SIZE: size = h (height), so
                     * increasing h makes the text larger. The text is centered
                     * inside the (x, y, w, h) box but is NOT auto-shrunk to fit.
                     * Approximate metrics: ~0.6em advance, ~1.2em line height. */
                    int len = (int)strlen(inst->text);
                    int size = inst->d;                 /* font size = h */
                    int text_w, text_h, tx, ty;
                    if (size < 1) size = 1;
                    if (size > 600) size = 600;
                    text_w = (int)(len * size * 0.6);
                    text_h = (int)(size * 1.2);
                    tx = inst->a + (inst->c - text_w) / 2;
                    ty = inst->b + (inst->d - text_h) / 2;
                    if (tx < inst->a) tx = inst->a;
                    if (ty < inst->b) ty = inst->b;
                    hosc_gui_draw_text_styled(tx, ty, inst->text, size, 24, 26, 32, false);
                } else {
                    hosc_gui_draw_text(inst->a, inst->b, inst->text);
                }
                *touched_gui = 1;
                pc++;
                break;
            case OP_RECT:
                hosc_gui_draw_rect(inst->a, inst->b, inst->c, inst->d,
                                   (int)inst->flags, inst->jump_index, inst->e,
                                   (inst->text[0] != 0));
                *touched_gui = 1;
                pc++;
                break;
            case OP_IMAGE:
                hosc_gui_draw_image(inst->a, inst->b, inst->c, inst->d, inst->text);
                *touched_gui = 1;
                pc++;
                break;
            case OP_BUTTON:
                draw_button(inst);
                *touched_gui = 1;
                pc++;
                break;
            case OP_SEEKBAR:
                draw_seekbar(inst);
                *touched_gui = 1;
                pc++;
                break;
            case OP_PLAY_SOUND:
                if ((inst->flags & FLAG_LEGACY_CALL) != 0U) {
                    static int warned_play_sound_legacy = 0;
                    if (!warned_play_sound_legacy) {
                        fprintf(stderr, "[deprecated] play_sound(\"...\") is legacy. Prefer audio.play({ src: \"...\" }).\n");
                        warned_play_sound_legacy = 1;
                    }
                }
                if (!hosc_audio_play_file(inst->text, true)) {
                    fprintf(stderr, "[audio] failed to play: %s\n", inst->text);
                }
                pc++;
                break;
            case OP_AUDIO_STOP:
                hosc_audio_stop();
                pc++;
                break;
            case OP_WIN32_MESSAGE_BOX:
                if ((inst->flags & FLAG_LEGACY_CALL) != 0U) {
                    static int warned_msgbox_legacy = 0;
                    if (!warned_msgbox_legacy) {
                        fprintf(stderr, "[deprecated] win32_message_box(\"...\") is legacy. Prefer system.messageBox({ content: \"...\" }).\n");
                        warned_msgbox_legacy = 1;
                    }
                }
#ifdef _WIN32
                {
                    UINT style = MB_OK;
                    if ((inst->flags & MSG_ICON_WARNING) != 0U) {
                        style |= MB_ICONWARNING;
                    } else if ((inst->flags & MSG_ICON_ERROR) != 0U) {
                        style |= MB_ICONERROR;
                    } else if ((inst->flags & MSG_ICON_INFO) != 0U) {
                        style |= MB_ICONINFORMATION;
                    }
                    MessageBoxA(NULL, inst->text, (inst->extra[0] ? inst->extra : "HOSC"), style);
                }
#else
                printf("[win32 unavailable] %s\n", inst->text);
#endif
                pc++;
                break;
            case OP_PUMP_EVENTS:
                frame_event_count = collect_frame_events(frame_events, MAX_FRAME_EVENTS, &should_quit);
                frame_events_valid = 1;
                pc++;
                break;
            case OP_EVENT_CLICK:
                if (!frame_events_valid) {
                    frame_event_count = collect_frame_events(frame_events, MAX_FRAME_EVENTS, &should_quit);
                    frame_events_valid = 1;
                }
                handle_click_events(inst, frame_events, frame_event_count, touched_gui);
                pc++;
                break;
            case OP_EVENT_KEY:
                if (!frame_events_valid) {
                    frame_event_count = collect_frame_events(frame_events, MAX_FRAME_EVENTS, &should_quit);
                    frame_events_valid = 1;
                }
                handle_key_events(inst, frame_events, frame_event_count, touched_gui);
                pc++;
                break;
            case OP_EVENT_MOUSE_MOVE:
                if (!frame_events_valid) {
                    frame_event_count = collect_frame_events(frame_events, MAX_FRAME_EVENTS, &should_quit);
                    frame_events_valid = 1;
                }
                handle_mouse_move_events(inst, frame_events, frame_event_count, touched_gui);
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
                    frame_event_count = collect_frame_events(frame_events, MAX_FRAME_EVENTS, &should_quit);
                    frame_events_valid = 1;

                    if (should_quit) {
                        break;
                    }

                    handle_button_clicks(program, frame_events, frame_event_count, touched_gui);
                    handle_seekbar_clicks(program, frame_events, frame_event_count, touched_gui);

                    if (hosc_gui_backend() != HOSC_GUI_BACKEND_CONSOLE && !hosc_gui_is_running()) {
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

                frame_event_count = collect_frame_events(frame_events, MAX_FRAME_EVENTS, &should_quit);
                frame_events_valid = 1;
                handle_media_player_clicks(program, frame_events, frame_event_count, touched_gui);
                handle_button_clicks(program, frame_events, frame_event_count, touched_gui);
                handle_seekbar_clicks(program, frame_events, frame_event_count, touched_gui);
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

static const char* opcode_name(OpCode opcode) {
    switch (opcode) {
        case OP_WINDOW: return "window";
        case OP_PRINT: return "print";
        case OP_THEME: return "theme";
        case OP_PANEL: return "panel";
        case OP_LABEL: return "label";
        case OP_MEDIA_PLAYER: return "media_player";
        case OP_TEXT: return "text";
        case OP_RECT: return "rect";
        case OP_IMAGE: return "image";
        case OP_BUTTON: return "button";
        case OP_SEEKBAR: return "seekbar";
        case OP_PLAY_SOUND: return "audio.play";
        case OP_AUDIO_STOP: return "audio.stop";
        case OP_WIN32_MESSAGE_BOX: return "system.messageBox";
        case OP_PUMP_EVENTS: return "pump_events";
        case OP_EVENT_CLICK: return "on_click";
        case OP_EVENT_KEY: return "on_key";
        case OP_EVENT_MOUSE_MOVE: return "on_mouse_move";
        case OP_LOOP_SIMPLE: return "loop";
        case OP_LOOP_BEGIN: return "loop_begin";
        case OP_LOOP_END: return "loop_end";
        default: return "unknown";
    }
}

static void build_output_path(const char* script_path, const char* extension, char* output_path, size_t output_cap) {
    const char* file_name = script_path;
    const char* dot = NULL;
    size_t i;

    if (!output_path || output_cap == 0) {
        return;
    }

    for (i = 0; script_path[i] != '\0'; i++) {
        if (script_path[i] == '\\' || script_path[i] == '/') {
            file_name = script_path + i + 1;
        }
    }

    dot = strrchr(file_name, '.');
    if (!dot || dot == file_name) {
        dot = file_name + strlen(file_name);
    }

    /* Forward slashes work on Windows as well as POSIX platforms. */
    snprintf(output_path, output_cap, "framework/build/%.*s.%s",
             (int)(dot - file_name), file_name, extension);
}

static void copy_cstr(char* dest, size_t dest_cap, const char* src) {
    size_t len;

    if (!dest || dest_cap == 0) {
        return;
    }

    if (!src) {
        dest[0] = '\0';
        return;
    }

    len = strlen(src);
    if (len >= dest_cap) {
        len = dest_cap - 1;
    }

    memcpy(dest, src, len);
    dest[len] = '\0';
}

static int write_build_artifact(const char* script_path, const Program* program, char* output_path, size_t output_cap) {
    FILE* file;
    int i;

    build_output_path(script_path, "hfb", output_path, output_cap);
    file = fopen(output_path, "w");
    if (!file) {
        fprintf(stderr, "Build error: cannot write artifact %s\n", output_path);
        return 0;
    }

    fprintf(file, "HOSC_FRAMEWORK_BUILD 1\n");
    fprintf(file, "source=%s\n", script_path);
    fprintf(file, "instructions=%d\n", program->count);

    for (i = 0; i < program->count; i++) {
        const Instruction* inst = &program->items[i];
        fprintf(file, "%04d %s a=%d b=%d c=%d d=%d e=%d flags=%u text=%s extra=%s\n",
                i,
                opcode_name(inst->opcode),
                inst->a, inst->b, inst->c, inst->d, inst->e,
                inst->flags,
                (inst->text[0] ? inst->text : ""),
                (inst->extra[0] ? inst->extra : ""));
    }

    fclose(file);
    return 1;
}

static int write_hbc_file(const char* script_path, const Program* program, const char* base_dir,
                          char* output_path, size_t output_cap) {
    FILE* file;
    HBCHeader header;

    build_output_path(script_path, "hbc", output_path, output_cap);
    file = fopen(output_path, "wb");
    if (!file) {
        fprintf(stderr, "Build error: cannot write bytecode %s\n", output_path);
        return 0;
    }

    memset(&header, 0, sizeof(header));
    memcpy(header.magic, "HOSC_HBC_V1", 11);
    header.version = 1U;
    header.program_size = (uint32_t)sizeof(*program);
    if (base_dir && base_dir[0]) {
        copy_cstr(header.base_dir, sizeof(header.base_dir), base_dir);
    }

    if (fwrite(&header, 1, sizeof(header), file) != sizeof(header) ||
        fwrite(program, 1, sizeof(*program), file) != sizeof(*program)) {
        fclose(file);
        fprintf(stderr, "Build error: failed to write bytecode %s\n", output_path);
        return 0;
    }

    fclose(file);
    return 1;
}

static int load_hbc_file(const char* path, Program* program, char* base_dir, size_t base_dir_cap) {
    FILE* file;
    HBCHeader header;

    file = fopen(path, "rb");
    if (!file) {
        fprintf(stderr, "Cannot open bytecode: %s\n", path);
        return 0;
    }

    if (fread(&header, 1, sizeof(header), file) != sizeof(header)) {
        fclose(file);
        fprintf(stderr, "Invalid .hbc file: %s\n", path);
        return 0;
    }

    if (memcmp(header.magic, "HOSC_HBC_V1", 11) != 0 ||
        header.version != 1U ||
        header.program_size != (uint32_t)sizeof(*program)) {
        fclose(file);
        fprintf(stderr, "Unsupported .hbc format: %s\n", path);
        return 0;
    }

    if (fread(program, 1, sizeof(*program), file) != sizeof(*program)) {
        fclose(file);
        fprintf(stderr, "Corrupted .hbc payload: %s\n", path);
        return 0;
    }

    if (base_dir && base_dir_cap > 0) {
        copy_cstr(base_dir, base_dir_cap, header.base_dir);
    }

    fclose(file);
    return 1;
}

static int copy_file_stream(FILE* in_file, FILE* out_file) {
    char buffer[8192];
    size_t bytes_read;

    while ((bytes_read = fread(buffer, 1, sizeof(buffer), in_file)) > 0) {
        if (fwrite(buffer, 1, bytes_read, out_file) != bytes_read) {
            return 0;
        }
    }

    return (ferror(in_file) == 0);
}

static int write_standalone_exe(const char* runtime_exe_path, const char* script_path,
                                const Program* program, const char* base_dir,
                                char* output_path, size_t output_cap) {
    FILE* in_file;
    FILE* out_file;
    EmbeddedProgramFooter footer;

    build_output_path(script_path, "exe", output_path, output_cap);

    in_file = fopen(runtime_exe_path, "rb");
    if (!in_file) {
        fprintf(stderr, "Build error: cannot open runtime executable %s\n", runtime_exe_path);
        return 0;
    }

    out_file = fopen(output_path, "wb");
    if (!out_file) {
        fclose(in_file);
        fprintf(stderr, "Build error: cannot write executable %s\n", output_path);
        return 0;
    }

    if (!copy_file_stream(in_file, out_file)) {
        fclose(in_file);
        fclose(out_file);
        fprintf(stderr, "Build error: failed to copy runtime executable into %s\n", output_path);
        return 0;
    }

    fclose(in_file);

    if (fwrite(program, 1, sizeof(*program), out_file) != sizeof(*program)) {
        fclose(out_file);
        fprintf(stderr, "Build error: failed to append embedded program to %s\n", output_path);
        return 0;
    }

    memset(&footer, 0, sizeof(footer));
    memcpy(footer.magic, EMBEDDED_PROGRAM_MAGIC, strlen(EMBEDDED_PROGRAM_MAGIC));
    footer.version = 1U;
    footer.program_size = (uint64_t)sizeof(*program);
    if (base_dir && base_dir[0]) {
        copy_cstr(footer.base_dir, sizeof(footer.base_dir), base_dir);
    }

    if (fwrite(&footer, 1, sizeof(footer), out_file) != sizeof(footer)) {
        fclose(out_file);
        fprintf(stderr, "Build error: failed to append embedded footer to %s\n", output_path);
        return 0;
    }

    fclose(out_file);
    return 1;
}

static int load_embedded_program(const char* exe_path, Program* program, char* base_dir, size_t base_dir_cap) {
    FILE* file;
    EmbeddedProgramFooter footer;
    long file_size;
    long program_offset;

    file = fopen(exe_path, "rb");
    if (!file) {
        return 0;
    }

    if (fseek(file, 0, SEEK_END) != 0) {
        fclose(file);
        return 0;
    }

    file_size = ftell(file);
    if (file_size < (long)sizeof(footer) + (long)sizeof(*program)) {
        fclose(file);
        return 0;
    }

    if (fseek(file, file_size - (long)sizeof(footer), SEEK_SET) != 0) {
        fclose(file);
        return 0;
    }

    if (fread(&footer, 1, sizeof(footer), file) != sizeof(footer)) {
        fclose(file);
        return 0;
    }

    if (memcmp(footer.magic, EMBEDDED_PROGRAM_MAGIC, strlen(EMBEDDED_PROGRAM_MAGIC)) != 0 ||
        footer.version != 1U ||
        footer.program_size != (uint64_t)sizeof(*program)) {
        fclose(file);
        return 0;
    }

    program_offset = file_size - (long)sizeof(footer) - (long)footer.program_size;
    if (program_offset < 0) {
        fclose(file);
        return 0;
    }

    if (fseek(file, program_offset, SEEK_SET) != 0) {
        fclose(file);
        return 0;
    }

    if (fread(program, 1, sizeof(*program), file) != sizeof(*program)) {
        fclose(file);
        return 0;
    }

    if (base_dir && base_dir_cap > 0) {
        copy_cstr(base_dir, base_dir_cap, footer.base_dir);
    }

    fclose(file);
    return 1;
}

static void capture_current_directory(char* output, size_t output_cap) {
    if (!output || output_cap == 0) {
        return;
    }

    output[0] = '\0';

#ifdef _WIN32
    if (GetCurrentDirectoryA((DWORD)output_cap, output) == 0) {
        output[0] = '\0';
    }
#else
    if (!getcwd(output, output_cap)) {
        output[0] = '\0';
    }
#endif
}

static void print_usage(const char* exe_name) {
    printf("Usage:\n");
    printf("  %s run <script.hosc|script.hbc>\n", exe_name);
    printf("  %s build <script.hosc>\n", exe_name);
    printf("\n");
    printf("Statements:\n");
    printf("  window({ title: \"Title\", width: 380, height: 465, resizable: false, fullscreen: false, icon: \"calc.png\", minWidth: 300, minHeight: 300, center: true });\n");
    printf("  window(\"Title\");  // legacy-compatible\n");
    printf("  print(\"Message\");\n");
    printf("  media_player({ x: 28, y: 24, width: 924, height: 532, title: \"Track\", cover: \"cover.png\", src: \"track.mp3\" });\n");
    printf("  text({ x: 20, y: 20, content: \"Message\" });\n");
    printf("  text(x, y, \"Message\");\n");
    printf("  rect({ x: 20, y: 20, width: 100, height: 40, r: 40, g: 40, b: 40, filled: true });\n");
    printf("  image(x, y, width, height, \"path\");\n");
    printf("  image({ x: 20, y: 50, width: 320, height: 320, src: \"asset.png\" });\n");
    printf("  button({ x: 20, y: 420, width: 100, height: 40, label: \"PLAY\", action: \"play\", src: \"asset.mp3\" });\n");
    printf("  seekbar({ x: 20, y: 380, width: 300, height: 10 });\n");
    printf("  audio.play({ src: \"path.mp3\" });\n");
    printf("  audio.stop();\n");
    printf("  play_sound(\"path.mp3\");\n");
    printf("  system.messageBox({ title: \"HOSC\", content: \"Message\", icon: \"info\" });\n");
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
    Program* program;
    int touched_gui = 0;
    int is_build = 0;
    int has_embedded_program = 0;
    char build_output[512];
    char artifact_output[512];
    char bytecode_output[512];
    char runtime_base_dir[1024];
    const char* input_script = NULL;
    const char* input_extension = NULL;

    program = (Program*)calloc(1, sizeof(Program));
    if (!program) {
        fprintf(stderr, "Failed to allocate Program\n");
        return 1;
    }
    memset(runtime_base_dir, 0, sizeof(runtime_base_dir));
    has_embedded_program = load_embedded_program(argv[0], program, runtime_base_dir, sizeof(runtime_base_dir));

    if (argc >= 3) {
        if (strcmp(argv[1], "run") != 0 && strcmp(argv[1], "build") != 0) {
            print_usage(argv[0]);
            free(program);
            return 1;
        }
        is_build = (strcmp(argv[1], "build") == 0);
        input_script = argv[2];
        input_extension = strrchr(input_script, '.');
        capture_current_directory(runtime_base_dir, sizeof(runtime_base_dir));
    } else if (!has_embedded_program) {
        print_usage(argv[0]);
        free(program);
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
        free(program);
        return 1;
    }

    hosc_load_module("core");
    hosc_load_module("gui");
    hosc_load_module("win32");
    hosc_runtime_set_base_dir(runtime_base_dir);

    printf("HOSC framework runtime started (GUI backend: %s)\n", hosc_gui_backend_name());

    if (!has_embedded_program) {
        if (input_extension && strcmp(input_extension, ".hbc") == 0) {
            if (!load_hbc_file(input_script, program, runtime_base_dir, sizeof(runtime_base_dir))) {
                hosc_runtime_shutdown(runtime);
                free(program);
                return 1;
            }
            hosc_runtime_set_base_dir(runtime_base_dir);
        } else {
            if (!parse_script_file(input_script, program)) {
                hosc_runtime_shutdown(runtime);
                free(program);
                return 1;
            }
        }
    }

    if (is_build) {
        if (!write_build_artifact(input_script, program, artifact_output, sizeof(artifact_output))) {
            hosc_runtime_shutdown(runtime);
            free(program);
            return 1;
        }
        if (!write_hbc_file(input_script, program, runtime_base_dir, bytecode_output, sizeof(bytecode_output))) {
            hosc_runtime_shutdown(runtime);
            free(program);
            return 1;
        }
        if (!write_standalone_exe(argv[0], input_script, program, runtime_base_dir, build_output, sizeof(build_output))) {
            hosc_runtime_shutdown(runtime);
            free(program);
            return 1;
        }
        printf("Build succeeded: %s\n", build_output);
        printf("Artifact: %s\n", artifact_output);
        printf("Bytecode: %s\n", bytecode_output);
        printf("Instructions: %d\n", program->count);
        hosc_runtime_shutdown(runtime);
        free(program);
        return 0;
    }

    if (!execute_program(program, &touched_gui)) {
        hosc_runtime_shutdown(runtime);
        free(program);
        return 1;
    }

    if (touched_gui && hosc_gui_backend() != HOSC_GUI_BACKEND_CONSOLE && hosc_gui_is_running()) {
        printf("Press Enter to close GUI window...\n");
        getchar();
    }

    hosc_runtime_shutdown(runtime);
    free(program);
    return 0;
}

