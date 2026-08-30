/*
 * File: compiler/src/sema/symbol_table.c
 * Purpose: HOSC Symbol Table implementation.
 */

#include <stdlib.h>
#include <string.h>
#include "hosc_symbol_table.h"

static int min_edit_distance(const char *s1, const char *s2) {
    int m = (int)strlen(s1);
    int n = (int)strlen(s2);
    int dp[64][64];
    int i, j;

    if (m > 60) m = 60;
    if (n > 60) n = 60;

    for (i = 0; i <= m; i++) dp[i][0] = i;
    for (j = 0; j <= n; j++) dp[0][j] = j;

    for (i = 1; i <= m; i++) {
        for (j = 1; j <= n; j++) {
            if (s1[i - 1] == s2[j - 1]) {
                dp[i][j] = dp[i - 1][j - 1];
            } else {
                int a = dp[i - 1][j] + 1;
                int b = dp[i][j - 1] + 1;
                int c = dp[i - 1][j - 1] + 1;
                int min = a < b ? a : b;
                dp[i][j] = min < c ? min : c;
            }
        }
    }
    return dp[m][n];
}

SymbolTable* hosc_symbol_table_create(SymbolTable *parent) {
    SymbolTable *table = (SymbolTable*)calloc(1, sizeof(SymbolTable));
    if (!table) return NULL;
    table->parent = parent;
    table->head = NULL;
    return table;
}

void hosc_symbol_table_free(SymbolTable *table) {
    SymbolEntry *curr;
    if (!table) return;
    curr = table->head;
    while (curr) {
        SymbolEntry *next = curr->next;
        if (curr->symbol.name) free(curr->symbol.name);
        if (curr->symbol.param_types) free(curr->symbol.param_types);
        free(curr);
        curr = next;
    }
    free(table);
}

static char* hosc_strdup(const char *s) {
    size_t len;
    char *dup;
    if (!s) return NULL;
    len = strlen(s);
    dup = (char*)malloc(len + 1);
    if (dup) memcpy(dup, s, len + 1);
    return dup;
}

bool hosc_symbol_table_define(SymbolTable *table, const char *name, SymbolKind kind, HoscType type, bool is_constant, size_t param_count, int line, int col) {
    SymbolEntry *entry;
    if (!table || !name) return false;
    if (hosc_symbol_table_lookup_current(table, name)) return false;

    entry = (SymbolEntry*)calloc(1, sizeof(SymbolEntry));
    if (!entry) return false;
    entry->symbol.name = hosc_strdup(name);

    entry->symbol.kind = kind;
    entry->symbol.type = type;
    entry->symbol.is_constant = is_constant;
    entry->symbol.param_count = param_count;
    entry->symbol.line = line;
    entry->symbol.col = col;

    entry->next = table->head;
    table->head = entry;
    return true;
}

Symbol* hosc_symbol_table_lookup_current(SymbolTable *table, const char *name) {
    SymbolEntry *curr;
    if (!table || !name) return NULL;
    curr = table->head;
    while (curr) {
        if (curr->symbol.name && strcmp(curr->symbol.name, name) == 0) {
            return &curr->symbol;
        }
        curr = curr->next;
    }
    return NULL;
}

Symbol* hosc_symbol_table_lookup(SymbolTable *table, const char *name) {
    SymbolTable *scope = table;
    while (scope) {
        Symbol *sym = hosc_symbol_table_lookup_current(scope, name);
        if (sym) return sym;
        scope = scope->parent;
    }
    return NULL;
}

const char* hosc_symbol_table_find_closest(SymbolTable *table, const char *name) {
    const char *best_name = NULL;
    int best_dist = 999;
    SymbolTable *scope = table;

    if (!name || !*name) return NULL;

    while (scope) {
        SymbolEntry *curr = scope->head;
        while (curr) {
            if (curr->symbol.name) {
                int dist = min_edit_distance(name, curr->symbol.name);
                if (dist < best_dist && dist <= 3) {
                    best_dist = dist;
                    best_name = curr->symbol.name;
                }
            }
            curr = curr->next;
        }
        scope = scope->parent;
    }
    return best_name;
}

const char* hosc_type_to_string(HoscType type) {
    switch (type) {
        case TYPE_INT: return "int";
        case TYPE_FLOAT: return "float";
        case TYPE_STRING: return "string";
        case TYPE_BOOL: return "bool";
        case TYPE_VOID: return "void";
        case TYPE_ANY: return "any";
        case TYPE_UNKNOWN: return "unknown";
        case TYPE_ERROR: return "error";
        default: return "unknown";
    }
}
