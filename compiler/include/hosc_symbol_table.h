/*
 * File: compiler/include/hosc_symbol_table.h
 * Purpose: HOSC Symbol Table and Type System interface.
 */

#pragma once
#ifdef __cplusplus
extern "C" {
#endif

#include <stddef.h>
#include <stdbool.h>

typedef enum {
    TYPE_UNKNOWN = 0,
    TYPE_INT,
    TYPE_FLOAT,
    TYPE_STRING,
    TYPE_BOOL,
    TYPE_VOID,
    TYPE_ANY,
    TYPE_ERROR
} HoscType;

typedef enum {
    SYMBOL_VAR,
    SYMBOL_CONST,
    SYMBOL_FUNC,
    SYMBOL_BUILTIN
} SymbolKind;

typedef struct Symbol {
    char *name;
    SymbolKind kind;
    HoscType type;
    bool is_constant;
    size_t param_count;
    HoscType *param_types;
    int line;
    int col;
} Symbol;

typedef struct SymbolEntry {
    Symbol symbol;
    struct SymbolEntry *next;
} SymbolEntry;

typedef struct SymbolTable {
    SymbolEntry *head;
    struct SymbolTable *parent;
} SymbolTable;

SymbolTable* hosc_symbol_table_create(SymbolTable *parent);
void hosc_symbol_table_free(SymbolTable *table);

bool hosc_symbol_table_define(SymbolTable *table, const char *name, SymbolKind kind, HoscType type, bool is_constant, size_t param_count, int line, int col);
Symbol* hosc_symbol_table_lookup(SymbolTable *table, const char *name);
Symbol* hosc_symbol_table_lookup_current(SymbolTable *table, const char *name);
const char* hosc_symbol_table_find_closest(SymbolTable *table, const char *name);
const char* hosc_type_to_string(HoscType type);

#ifdef __cplusplus
}
#endif
