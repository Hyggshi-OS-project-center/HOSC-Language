#include "hvm_value.h"

#include <stdio.h>

#include "hvm_object.h"

HValue hvm_value_nil(void) {
    HValue value;
    value.tag = HVAL_NIL;
    return value;
}

HValue hvm_value_bool(bool boolean) {
    HValue value;
    value.tag = HVAL_BOOL;
    value.as.boolean = boolean;
    return value;
}

HValue hvm_value_int(int64_t integer) {
    HValue value;
    value.tag = HVAL_INT;
    value.as.integer = integer;
    return value;
}

HValue hvm_value_float(double floating) {
    HValue value;
    value.tag = HVAL_FLOAT;
    value.as.floating = floating;
    return value;
}

HValue hvm_value_object(HObject* object) {
    HValue value;
    value.tag = HVAL_OBJ;
    value.as.object = object;
    return value;
}

bool hvm_value_is_truthy(HValue value) {
    switch (value.tag) {
        case HVAL_NIL: return false;
        case HVAL_BOOL: return value.as.boolean;
        default: return true;
    }
}

bool hvm_value_equals(HValue left, HValue right) {
    if (left.tag != right.tag) {
        return false;
    }

    switch (left.tag) {
        case HVAL_NIL: return true;
        case HVAL_BOOL: return left.as.boolean == right.as.boolean;
        case HVAL_INT: return left.as.integer == right.as.integer;
        case HVAL_FLOAT: return left.as.floating == right.as.floating;
        case HVAL_OBJ: return left.as.object == right.as.object;
        default: return false;
    }
}

void hvm_value_print(HValue value) {
    switch (value.tag) {
        case HVAL_NIL:
            fputs("nil", stdout);
            break;
        case HVAL_BOOL:
            fputs(value.as.boolean ? "true" : "false", stdout);
            break;
        case HVAL_INT:
            fprintf(stdout, "%lld", (long long)value.as.integer);
            break;
        case HVAL_FLOAT:
            fprintf(stdout, "%f", value.as.floating);
            break;
        case HVAL_OBJ:
            if (value.as.object && value.as.object->kind == HOBJ_STRING) {
                fputs(((HStringObject*)value.as.object)->chars, stdout);
            } else if (value.as.object && value.as.object->kind == HOBJ_NATIVE) {
                fprintf(stdout, "<native:%s>", ((HNativeObject*)value.as.object)->name);
            } else {
                fputs("<object>", stdout);
            }
            break;
        default:
            fputs("<unknown>", stdout);
            break;
    }
}
