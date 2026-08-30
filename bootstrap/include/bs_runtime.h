/*
 * HOSC Bootstrap Compiler & Interpreter
 * File: bootstrap/include/bs_runtime.h
 * Purpose: Runtime environment, variable scopes, and C API command registration
 */

#ifndef HOSC_BOOTSTRAP_RUNTIME_H
#define HOSC_BOOTSTRAP_RUNTIME_H

#include "bs_value.h"
#include "bs_command_registry.h"
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct BsEnvEntry {
    char *name;
    BsValue value;
    bool is_const;
    struct BsEnvEntry *next;
} BsEnvEntry;

typedef struct BsEnvironment {
    BsEnvEntry *head;
    struct BsEnvironment *parent;
} BsEnvironment;

typedef struct BsRuntime {
    BsEnvironment *global_env;
    BsEnvironment *current_env;
    BsCommandRegistry command_registry;
    bool runtime_error;
    char error_message[256];
} BsRuntime;

/* Runtime lifecycle */
BsRuntime* bs_runtime_create(void);
void bs_runtime_free(BsRuntime *runtime);

/* Scope management */
BsEnvironment* bs_env_create(BsEnvironment *parent);
void bs_env_free(BsEnvironment *env);
bool bs_env_define(BsEnvironment *env, const char *name, BsValue val, bool is_const);
bool bs_env_assign(BsEnvironment *env, const char *name, BsValue val);
bool bs_env_get(BsEnvironment *env, const char *name, BsValue *out_val);

/* Scope pushing/popping in runtime */
void bs_runtime_push_scope(BsRuntime *runtime);
void bs_runtime_pop_scope(BsRuntime *runtime);

/* C API: Register native command into runtime */
bool bootstrap_register_command(
    BsRuntime *runtime,
    const char *name,
    size_t parameter_count,
    BsNativeCommand callback
);

/* Builtin registrations */
void bs_runtime_register_builtins(BsRuntime *runtime);

#ifdef __cplusplus
}
#endif

#endif /* HOSC_BOOTSTRAP_RUNTIME_H */
