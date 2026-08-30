/*
 * HOSC Bootstrap Compiler & Interpreter
 * File: bootstrap/src/bs_value.c
 * Purpose: Implementation of BsValue creation, destruction, copying, and equality
 */

#include "bs_value.h"
#include "bs_ast.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

BsValue bs_null(void) {
    BsValue v;
    v.type = BS_VAL_NULL;
    v.as.integer = 0;
    return v;
}

BsValue bs_bool(bool val) {
    BsValue v;
    v.type = BS_VAL_BOOL;
    v.as.boolean = val;
    return v;
}

BsValue bs_int(int64_t val) {
    BsValue v;
    v.type = BS_VAL_INT;
    v.as.integer = val;
    return v;
}

BsValue bs_float(double val) {
    BsValue v;
    v.type = BS_VAL_FLOAT;
    v.as.floating = val;
    return v;
}

BsValue bs_string(const char *val) {
    BsValue v;
    v.type = BS_VAL_STRING;
    if (val) {
        size_t len = strlen(val);
        v.as.string = (char*)malloc(len + 1);
        if (v.as.string) {
            memcpy(v.as.string, val, len + 1);
        }
    } else {
        v.as.string = (char*)calloc(1, 1);
    }
    return v;
}

BsValue bs_string_take(char *val) {
    BsValue v;
    v.type = BS_VAL_STRING;
    v.as.string = val ? val : (char*)calloc(1, 1);
    return v;
}

BsValue bs_native_ptr(void *ptr, const char *type_tag) {
    BsValue v;
    v.type = BS_VAL_NATIVE_PTR;
    v.as.native_ptr.ptr = ptr;
    v.as.native_ptr.type_tag = type_tag;
    return v;
}

BsValue bs_function(const char *name, size_t param_count, char **param_names, ASTNode *body) {
    BsValue v;
    v.type = BS_VAL_FUNCTION;
    v.as.function.name = name ? strdup(name) : NULL;
    v.as.function.param_count = param_count;
    if (param_count > 0 && param_names) {
        v.as.function.param_names = (char**)malloc(sizeof(char*) * param_count);
        for (size_t i = 0; i < param_count; i++) {
            v.as.function.param_names[i] = param_names[i] ? strdup(param_names[i]) : NULL;
        }
    } else {
        v.as.function.param_names = NULL;
    }
    v.as.function.body = bs_ast_clone(body);
    return v;
}

BsValue bs_clone_value(BsValue v) {
    switch (v.type) {
        case BS_VAL_STRING:
            return bs_string(v.as.string);
        case BS_VAL_FUNCTION:
            return bs_function(v.as.function.name, v.as.function.param_count, v.as.function.param_names, v.as.function.body);
        default:
            return v;
    }
}

void bs_free_value(BsValue v) {
    if (v.type == BS_VAL_STRING) {
        if (v.as.string) {
            free(v.as.string);
        }
    } else if (v.type == BS_VAL_FUNCTION) {
        if (v.as.function.name) free(v.as.function.name);
        if (v.as.function.param_names) {
            for (size_t i = 0; i < v.as.function.param_count; i++) {
                if (v.as.function.param_names[i]) free(v.as.function.param_names[i]);
            }
            free(v.as.function.param_names);
        }
        if (v.as.function.body) {
            bs_ast_free(v.as.function.body);
        }
    }
}

void bs_print_value(BsValue v) {
    switch (v.type) {
        case BS_VAL_NULL:
            printf("null");
            break;
        case BS_VAL_BOOL:
            printf("%s", v.as.boolean ? "true" : "false");
            break;
        case BS_VAL_INT:
            printf("%ld", (long)v.as.integer);
            break;
        case BS_VAL_FLOAT:
            printf("%g", v.as.floating);
            break;
        case BS_VAL_STRING:
            printf("%s", v.as.string ? v.as.string : "");
            break;
        case BS_VAL_FUNCTION:
            printf("<function %s>", v.as.function.name ? v.as.function.name : "anonymous");
            break;
        case BS_VAL_NATIVE_PTR:
            printf("<native_ptr %s:%p>", v.as.native_ptr.type_tag ? v.as.native_ptr.type_tag : "void", v.as.native_ptr.ptr);
            break;
    }
}

bool bs_values_equal(BsValue a, BsValue b) {
    if (a.type != b.type) return false;
    switch (a.type) {
        case BS_VAL_NULL:
            return true;
        case BS_VAL_BOOL:
            return a.as.boolean == b.as.boolean;
        case BS_VAL_INT:
            return a.as.integer == b.as.integer;
        case BS_VAL_FLOAT:
            return a.as.floating == b.as.floating;
        case BS_VAL_STRING:
            if (!a.as.string && !b.as.string) return true;
            if (!a.as.string || !b.as.string) return false;
            return strcmp(a.as.string, b.as.string) == 0;
        case BS_VAL_NATIVE_PTR:
            return a.as.native_ptr.ptr == b.as.native_ptr.ptr;
        case BS_VAL_FUNCTION:
            return a.as.function.body == b.as.function.body;
    }
    return false;
}

bool bs_is_truthy(BsValue v) {
    switch (v.type) {
        case BS_VAL_NULL:
            return false;
        case BS_VAL_BOOL:
            return v.as.boolean;
        case BS_VAL_INT:
            return v.as.integer != 0;
        case BS_VAL_FLOAT:
            return v.as.floating != 0.0;
        case BS_VAL_STRING:
            return v.as.string && v.as.string[0] != '\0';
        default:
            return true;
    }
}

const char* bs_type_name(BsValueType type) {
    switch (type) {
        case BS_VAL_NULL: return "null";
        case BS_VAL_BOOL: return "bool";
        case BS_VAL_INT: return "int";
        case BS_VAL_FLOAT: return "float";
        case BS_VAL_STRING: return "string";
        case BS_VAL_FUNCTION: return "function";
        case BS_VAL_NATIVE_PTR: return "native_ptr";
    }
    return "unknown";
}
