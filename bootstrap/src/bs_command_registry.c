/*
 * HOSC Bootstrap Compiler & Interpreter
 * File: bootstrap/src/bs_command_registry.c
 * Purpose: Extensible command registry implementation
 */

#include "bs_command_registry.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void bs_command_registry_init(BsCommandRegistry *registry) {
    registry->commands = NULL;
    registry->count = 0;
    registry->capacity = 0;
}

static void free_command(BsCommand *cmd) {
    if (!cmd) return;
    if (cmd->name) free(cmd->name);
    if (cmd->param_names) {
        for (size_t i = 0; i < cmd->parameter_count; i++) {
            if (cmd->param_names[i]) free(cmd->param_names[i]);
        }
        free(cmd->param_names);
    }
    if (cmd->body) {
        bs_ast_free(cmd->body);
    }
    free(cmd);
}

void bs_command_registry_free(BsCommandRegistry *registry) {
    if (!registry) return;
    for (size_t i = 0; i < registry->count; i++) {
        free_command(registry->commands[i]);
    }
    if (registry->commands) free(registry->commands);
    registry->commands = NULL;
    registry->count = 0;
    registry->capacity = 0;
}

static void registry_add_internal(BsCommandRegistry *reg, BsCommand *cmd) {
    // If command with same name exists, replace it
    for (size_t i = 0; i < reg->count; i++) {
        if (strcmp(reg->commands[i]->name, cmd->name) == 0) {
            free_command(reg->commands[i]);
            reg->commands[i] = cmd;
            return;
        }
    }

    if (reg->count + 1 > reg->capacity) {
        size_t new_cap = reg->capacity < 8 ? 8 : reg->capacity * 2;
        reg->commands = (BsCommand**)realloc(reg->commands, sizeof(BsCommand*) * new_cap);
        reg->capacity = new_cap;
    }
    reg->commands[reg->count++] = cmd;
}

bool bs_register_ast_command(BsCommandRegistry *reg, const char *name, size_t param_count, char **param_names, ASTNode *body) {
    if (!reg || !name) return false;
    BsCommand *cmd = (BsCommand*)calloc(1, sizeof(BsCommand));
    cmd->name = strdup(name);
    cmd->parameter_count = param_count;
    cmd->kind = COMMAND_AST;
    if (param_count > 0 && param_names) {
        cmd->param_names = (char**)malloc(sizeof(char*) * param_count);
        for (size_t i = 0; i < param_count; i++) {
            cmd->param_names[i] = param_names[i] ? strdup(param_names[i]) : NULL;
        }
    }
    cmd->body = bs_ast_clone(body);
    cmd->native = NULL;
    registry_add_internal(reg, cmd);
    return true;
}

bool bs_register_macro(BsCommandRegistry *reg, const char *name, size_t param_count, char **param_names, ASTNode *body) {
    if (!reg || !name) return false;
    BsCommand *cmd = (BsCommand*)calloc(1, sizeof(BsCommand));
    cmd->name = strdup(name);
    cmd->parameter_count = param_count;
    cmd->kind = COMMAND_MACRO;
    if (param_count > 0 && param_names) {
        cmd->param_names = (char**)malloc(sizeof(char*) * param_count);
        for (size_t i = 0; i < param_count; i++) {
            cmd->param_names[i] = param_names[i] ? strdup(param_names[i]) : NULL;
        }
    }
    cmd->body = bs_ast_clone(body);
    cmd->native = NULL;
    registry_add_internal(reg, cmd);
    return true;
}

bool bs_register_native_command(BsCommandRegistry *reg, const char *name, size_t param_count, BsNativeCommand callback) {
    if (!reg || !name || !callback) return false;
    BsCommand *cmd = (BsCommand*)calloc(1, sizeof(BsCommand));
    cmd->name = strdup(name);
    cmd->parameter_count = param_count;
    cmd->kind = COMMAND_NATIVE;
    cmd->param_names = NULL;
    cmd->body = NULL;
    cmd->native = callback;
    registry_add_internal(reg, cmd);
    return true;
}

BsCommand* bs_command_registry_lookup(BsCommandRegistry *reg, const char *name) {
    if (!reg || !name) return NULL;
    for (size_t i = 0; i < reg->count; i++) {
        if (strcmp(reg->commands[i]->name, name) == 0) {
            return reg->commands[i];
        }
    }
    return NULL;
}

bool bs_command_registry_has(BsCommandRegistry *reg, const char *name) {
    return bs_command_registry_lookup(reg, name) != NULL;
}
