#include "hosc_runtime_api.h"

#include "hvm_loader.h"

bool hosc_runtime_load_hbc_file(const char* path, HBytecode* out_bytecode, char* error_message, size_t error_message_size) {
    return hvm_bytecode_load_file(path, out_bytecode, error_message, error_message_size);
}
