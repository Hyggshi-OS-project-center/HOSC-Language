#ifndef HVM_LOADER_H
#define HVM_LOADER_H

#include <stdbool.h>

#include "hosc_bytecode.h"

bool hvm_bytecode_load_file(const char* path, HBytecode* out_bytecode, char* error_message, size_t error_message_size);

#endif
