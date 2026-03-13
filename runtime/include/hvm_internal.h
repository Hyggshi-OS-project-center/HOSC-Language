/* hvm_internal.h - Internal helpers for HOSC VM implementation */
#ifndef HVM_INTERNAL_H
#define HVM_INTERNAL_H

#include "hvm.h"

#ifdef __cplusplus
extern "C" {
#endif

void hvm_free_value(HVM_Value *v);
void hvm_set_error_msg(HVM_VM *vm, const char *msg);

char* hvm_ensure_buffer(char **buffer, size_t *cap, size_t needed);
const char* hvm_value_to_cstring(HVM_VM *vm, HVM_Value v, char **buffer, size_t *cap);
const char* hvm_value_to_cstring_a(HVM_VM *vm, HVM_Value v);
const char* hvm_value_to_cstring_b(HVM_VM *vm, HVM_Value v);
const char* hvm_concat_cstrings(HVM_VM *vm, const char *a, const char *b);

#ifdef __cplusplus
}
#endif

#endif /* HVM_INTERNAL_H */

