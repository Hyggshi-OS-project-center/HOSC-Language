/*
 * HOSC Bootstrap — Native Command: draw_rect
 * File: bootstrap/src/commands/draw_rect.c
 * Implements the typed draw_rect(x, y, width, height, color) command.
 *
 * BsValue type access reference:
 *   args[i].type                     — BS_VAL_NULL | BS_VAL_BOOL | BS_VAL_INT
 *                                       BS_VAL_FLOAT | BS_VAL_STRING | BS_VAL_FUNCTION
 *   args[i].as.integer  (int64_t)    — when type == BS_VAL_INT
 *   args[i].as.floating (double)     — when type == BS_VAL_FLOAT
 *   args[i].as.string   (char*)      — when type == BS_VAL_STRING
 *   args[i].as.boolean  (bool)       — when type == BS_VAL_BOOL
 */

#define _POSIX_C_SOURCE 200809L
#include "commands/draw_rect.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ─────────────────────────────────────────────────────────────────────────
 * bs_native_draw_rect
 *
 * HOSC call site:
 *   draw_rect(x, y, width, height, color);
 *
 * Parameters (5):
 *   args[0] — x      : BS_VAL_INT   — x position in pixels
 *   args[1] — y      : BS_VAL_INT   — y position in pixels
 *   args[2] — width  : BS_VAL_INT   — rectangle width
 *   args[3] — height : BS_VAL_INT   — rectangle height
 *   args[4] — color  : BS_VAL_STRING — CSS-style color ("red", "#ff0000")
 *
 * Returns: bs_bool(true) on success, bs_null() on type error
 * ───────────────────────────────────────────────────────────────────────── */
BsValue bs_native_draw_rect(BsRuntime *runtime, BsValue *args, size_t argc) {
    (void)runtime;

    /* ── Arity guard ────────────────────────────────────────────────────── */
    if (argc < 5) {
        fprintf(stderr, "[draw_rect] expected 5 arg(s), got %zu\n", argc);
        return bs_null();
    }

    /* ── Type checking — always before union access ─────────────────────── */
    if (args[0].type != BS_VAL_INT) {
        fprintf(stderr, "[draw_rect] arg 'x' (index 0) must be INT\n");
        return bs_null();
    }
    if (args[1].type != BS_VAL_INT) {
        fprintf(stderr, "[draw_rect] arg 'y' (index 1) must be INT\n");
        return bs_null();
    }
    if (args[2].type != BS_VAL_INT) {
        fprintf(stderr, "[draw_rect] arg 'width' (index 2) must be INT\n");
        return bs_null();
    }
    if (args[3].type != BS_VAL_INT) {
        fprintf(stderr, "[draw_rect] arg 'height' (index 3) must be INT\n");
        return bs_null();
    }
    if (args[4].type != BS_VAL_STRING) {
        fprintf(stderr, "[draw_rect] arg 'color' (index 4) must be STRING\n");
        return bs_null();
    }

    /* ── Typed bindings — safe after type checks above ──────────────────── */
    int64_t     x      = args[0].as.integer;
    int64_t     y      = args[1].as.integer;
    int64_t     width  = args[2].as.integer;
    int64_t     height = args[3].as.integer;
    const char *color  = args[4].as.string;

    /* ── Logic ─────────────────────────────────────────────────────────── */
    printf("[draw_rect] x=%lld y=%lld width=%lld height=%lld color=%s\n",
           (long long)x, (long long)y,
           (long long)width, (long long)height,
           color);

    return bs_bool(true); /* success */
}

/* Register this command into a BsRuntime */
void draw_rect_register(BsRuntime *runtime) {
    bootstrap_register_command(runtime, "draw_rect", 5, bs_native_draw_rect);
}
