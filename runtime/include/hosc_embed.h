#ifndef HOSC_EMBED_H
#define HOSC_EMBED_H

#include <stdbool.h>

#include "hosc_bytecode.h"
#include "hvm_api.h"

typedef struct HoscRuntime {
    HVM* vm;
} HoscRuntime;

HoscRuntime* hosc_runtime_create(void);
void hosc_runtime_destroy(HoscRuntime* runtime);
bool hosc_runtime_register_native(HoscRuntime* runtime, const char* name, int arity, HNativeFn fn);
int hosc_runtime_execute(HoscRuntime* runtime, const HBytecode* bytecode);

#endif
