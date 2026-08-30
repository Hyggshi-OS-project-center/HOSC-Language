/*
 * HOSC Bootstrap Compiler & Interpreter
 * File: bootstrap/src/bs_runtime.c
 * Purpose: Runtime environment, variable scope chain, and C API command registration
 */

#include "bs_runtime.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "commands/make_window.h"
#include "commands/play_sound.h"
#include "commands/send_packet.h"
#include "commands/draw_rect.h"

BsEnvironment* bs_env_create(BsEnvironment *parent) {
    BsEnvironment *env = (BsEnvironment*)calloc(1, sizeof(BsEnvironment));
    env->parent = parent;
    env->head = NULL;
    return env;
}

void bs_env_free(BsEnvironment *env) {
    if (!env) return;
    BsEnvEntry *entry = env->head;
    while (entry) {
        BsEnvEntry *next = entry->next;
        if (entry->name) free(entry->name);
        bs_free_value(entry->value);
        free(entry);
        entry = next;
    }
    free(env);
}

bool bs_env_define(BsEnvironment *env, const char *name, BsValue val, bool is_const) {
    if (!env || !name) return false;

    /* Check if already defined in current immediate scope */
    BsEnvEntry *curr = env->head;
    while (curr) {
        if (strcmp(curr->name, name) == 0) {
            bs_free_value(curr->value);
            curr->value = bs_clone_value(val);
            curr->is_const = is_const;
            return true;
        }
        curr = curr->next;
    }

    /* Add new entry at head */
    BsEnvEntry *entry = (BsEnvEntry*)calloc(1, sizeof(BsEnvEntry));
    entry->name = strdup(name);
    entry->value = bs_clone_value(val);
    entry->is_const = is_const;
    entry->next = env->head;
    env->head = entry;
    return true;
}

bool bs_env_assign(BsEnvironment *env, const char *name, BsValue val) {
    if (!name) return false;
    BsEnvironment *curr = env;
    while (curr) {
        BsEnvEntry *entry = curr->head;
        while (entry) {
            if (strcmp(entry->name, name) == 0) {
                if (entry->is_const) {
                    return false; /* Cannot reassign constant */
                }
                bs_free_value(entry->value);
                entry->value = bs_clone_value(val);
                return true;
            }
            entry = entry->next;
        }
        curr = curr->parent;
    }
    return false;
}

bool bs_env_get(BsEnvironment *env, const char *name, BsValue *out_val) {
    if (!name) return false;
    BsEnvironment *curr = env;
    while (curr) {
        BsEnvEntry *entry = curr->head;
        while (entry) {
            if (strcmp(entry->name, name) == 0) {
                if (out_val) {
                    *out_val = bs_clone_value(entry->value);
                }
                return true;
            }
            entry = entry->next;
        }
        curr = curr->parent;
    }
    return false;
}

BsRuntime* bs_runtime_create(void) {
    BsRuntime *rt = (BsRuntime*)calloc(1, sizeof(BsRuntime));
    rt->global_env = bs_env_create(NULL);
    rt->current_env = rt->global_env;
    bs_command_registry_init(&rt->command_registry);
    rt->runtime_error = false;
    rt->error_message[0] = '\0';

    bs_runtime_register_builtins(rt);
    return rt;
}

void bs_runtime_free(BsRuntime *runtime) {
    if (!runtime) return;
    bs_command_registry_free(&runtime->command_registry);
    BsEnvironment *curr = runtime->current_env;
    while (curr && curr != runtime->global_env) {
        BsEnvironment *p = curr->parent;
        bs_env_free(curr);
        curr = p;
    }
    if (runtime->global_env) {
        bs_env_free(runtime->global_env);
    }
    free(runtime);
}

void bs_runtime_push_scope(BsRuntime *runtime) {
    if (!runtime) return;
    runtime->current_env = bs_env_create(runtime->current_env);
}

void bs_runtime_pop_scope(BsRuntime *runtime) {
    if (!runtime || !runtime->current_env || runtime->current_env == runtime->global_env) return;
    BsEnvironment *parent = runtime->current_env->parent;
    bs_env_free(runtime->current_env);
    runtime->current_env = parent;
}

/* C API Implementation */
bool bootstrap_register_command(
    BsRuntime *runtime,
    const char *name,
    size_t parameter_count,
    BsNativeCommand callback
) {
    if (!runtime || !name || !callback) return false;
    return bs_register_native_command(&runtime->command_registry, name, parameter_count, callback);
}

/* Builtin native commands */
static BsValue builtin_clock(BsRuntime *runtime, BsValue *args, size_t argc) {
    (void)runtime; (void)args; (void)argc;
    clock_t c = clock();
    double sec = (double)c / (double)CLOCKS_PER_SEC;
    return bs_float(sec);
}

static BsValue builtin_callwindow(BsRuntime *runtime, BsValue *args, size_t argc) {
    (void)runtime;
    printf("[HOSC Native Window] Creating window instance");
    if (argc > 0 && args[0].type == BS_VAL_STRING) {
        printf(" (title/action: \"%s\")", args[0].as.string);
    }
    printf("\n");
    return bs_null();
}

static BsValue builtin_log(BsRuntime *runtime, BsValue *args, size_t argc) {
    (void)runtime;
    printf("[LOG] ");
    for (size_t i = 0; i < argc; i++) {
        bs_print_value(args[i]);
        if (i + 1 < argc) printf(" ");
    }
    printf("\n");
    return bs_null();
}

static BsValue builtin_assert_eq(BsRuntime *runtime, BsValue *args, size_t argc) {
    (void)runtime;
    if (argc < 2) {
        fprintf(stderr, "assert_eq requires 2 arguments\n");
        return bs_bool(false);
    }
    if (!bs_values_equal(args[0], args[1])) {
        fprintf(stderr, "Assertion failed: expected ");
        bs_print_value(args[0]);
        fprintf(stderr, " == ");
        bs_print_value(args[1]);
        fprintf(stderr, "\n");
        return bs_bool(false);
    }
    return bs_bool(true);
}

void bs_runtime_register_builtins(BsRuntime *runtime) {
    bootstrap_register_command(runtime, "clock", 0, builtin_clock);
    bootstrap_register_command(runtime, "callwindow", 1, builtin_callwindow);
    bootstrap_register_command(runtime, "log", 1, builtin_log);
    bootstrap_register_command(runtime, "assert_eq", 2, builtin_assert_eq);
    make_window_register(runtime);
    play_sound_register(runtime);
    send_packet_register(runtime);
    draw_rect_register(runtime);
}
