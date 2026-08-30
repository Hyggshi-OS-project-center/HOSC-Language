/*
 * HOSC Bootstrap Compiler & Interpreter
 * File: bootstrap/include/bs_command_registry.h
 * Purpose: Extensible Command Registry managing AST commands, native C callbacks, and macros
 */

#ifndef HOSC_BOOTSTRAP_COMMAND_REGISTRY_H
#define HOSC_BOOTSTRAP_COMMAND_REGISTRY_H

#include "bs_ast.h"
#include "bs_value.h"
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    COMMAND_AST,      /* Behavioral extension with AST body executed in runtime */
    COMMAND_NATIVE,   /* Native C function registered via C API */
    COMMAND_MACRO     /* Syntax / AST transformation macro expanded before runtime */
} CommandKind;

typedef struct BsCommand {
    char *name;
    size_t parameter_count;
    char **param_names;
    CommandKind kind;

    ASTNode *body;          /* AST body for AST commands and Macros */
    BsNativeCommand native; /* C function pointer for Native commands */
} BsCommand;

typedef struct BsCommandRegistry {
    BsCommand **commands;
    size_t count;
    size_t capacity;
} BsCommandRegistry;

/* Registry lifecycle */
void bs_command_registry_init(BsCommandRegistry *registry);
void bs_command_registry_free(BsCommandRegistry *registry);

/* Registration functions */
bool bs_register_ast_command(BsCommandRegistry *reg, const char *name, size_t param_count, char **param_names, ASTNode *body);
bool bs_register_macro(BsCommandRegistry *reg, const char *name, size_t param_count, char **param_names, ASTNode *body);
bool bs_register_native_command(BsCommandRegistry *reg, const char *name, size_t param_count, BsNativeCommand callback);

/* Lookup */
BsCommand* bs_command_registry_lookup(BsCommandRegistry *reg, const char *name);
bool bs_command_registry_has(BsCommandRegistry *reg, const char *name);

#ifdef __cplusplus
}
#endif

#endif /* HOSC_BOOTSTRAP_COMMAND_REGISTRY_H */
