/*
 * HOSC Bootstrap Compiler & Interpreter
 * File: bootstrap/include/bs_value.h
 * Purpose: Value representation (dynamic types, numbers, strings, booleans, objects, functions)
 */

#ifndef HOSC_BOOTSTRAP_VALUE_H
#define HOSC_BOOTSTRAP_VALUE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    BS_VAL_NULL,
    BS_VAL_BOOL,
    BS_VAL_INT,
    BS_VAL_FLOAT,
    BS_VAL_STRING,
    BS_VAL_FUNCTION,
    BS_VAL_NATIVE_PTR
} BsValueType;

struct ASTNode;
struct BsRuntime;

typedef struct BsValue BsValue;

typedef BsValue (*BsNativeCommand)(struct BsRuntime *runtime, BsValue *args, size_t argc);

struct BsValue {
    BsValueType type;
    union {
        bool boolean;
        int64_t integer;
        double floating;
        char *string;
        struct {
            char *name;
            size_t param_count;
            char **param_names;
            struct ASTNode *body;
        } function;
        struct {
            void *ptr;
            const char *type_tag;
        } native_ptr;
    } as;
};

/* Value constructors */
BsValue bs_null(void);
BsValue bs_bool(bool val);
BsValue bs_int(int64_t val);
BsValue bs_float(double val);
BsValue bs_string(const char *val);
BsValue bs_string_take(char *val);
BsValue bs_native_ptr(void *ptr, const char *type_tag);
BsValue bs_function(const char *name, size_t param_count, char **param_names, struct ASTNode *body);

/* Memory and printing */
BsValue bs_clone_value(BsValue v);
void bs_free_value(BsValue v);
void bs_print_value(BsValue v);
bool bs_values_equal(BsValue a, BsValue b);
bool bs_is_truthy(BsValue v);
const char* bs_type_name(BsValueType type);

#ifdef __cplusplus
}
#endif

#endif /* HOSC_BOOTSTRAP_VALUE_H */
