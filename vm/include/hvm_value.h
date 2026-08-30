#ifndef HVM_VALUE_H
#define HVM_VALUE_H

#include <stdbool.h>
#include <stdint.h>

typedef struct HObject HObject;

typedef enum HValueTag {
    HVAL_NIL = 0,
    HVAL_BOOL = 1,
    HVAL_INT = 2,
    HVAL_FLOAT = 3,
    HVAL_OBJ = 4
} HValueTag;

typedef struct HValue {
    HValueTag tag;
    union {
        bool boolean;
        int64_t integer;
        double floating;
        HObject* object;
    } as;
} HValue;

HValue hvm_value_nil(void);
HValue hvm_value_bool(bool value);
HValue hvm_value_int(int64_t value);
HValue hvm_value_float(double value);
HValue hvm_value_object(HObject* object);
bool hvm_value_is_truthy(HValue value);
bool hvm_value_equals(HValue left, HValue right);
void hvm_value_print(HValue value);

#endif
