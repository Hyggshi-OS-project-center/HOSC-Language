#include "hvm_loader.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define HBC_MAX_SECTION_COUNT 65536u
#define HBC_MAX_STRING_LENGTH (16u * 1024u * 1024u)
#define HBC_MAX_CODE_SIZE (64u * 1024u * 1024u)

static void hvm_loader_error(char* error_message, size_t error_message_size, const char* message) {
    if (error_message && error_message_size > 0) {
        snprintf(error_message, error_message_size, "%s", message);
    }
}

static bool hvm_checked_section_size(uint32_t count, size_t item_size, size_t* out_size) {
    if (!out_size) {
        return false;
    }
    if (item_size != 0 && (size_t)count > ((size_t)-1) / item_size) {
        return false;
    }
    *out_size = (size_t)count * item_size;
    return true;
}

static bool hvm_file_has_remaining(long file_size, long offset, size_t needed) {
    if (offset < 0 || file_size < offset) {
        return false;
    }
    return needed <= (size_t)(file_size - offset);
}

static bool hvm_read_section(FILE* file, void* target, size_t size, long* offset) {
    if (size == 0) {
        return true;
    }
    if (!file || !target || !offset) {
        return false;
    }
    if (fread(target, 1, size, file) != size) {
        return false;
    }
    *offset += (long)size;
    return true;
}

static bool hvm_validate_loaded_bytecode(const HBytecode* bc, char* error_message, size_t error_message_size) {
    size_t i;

    if (!bc || bc->function_count == 0 || bc->entry_function_index >= bc->function_count) {
        hvm_loader_error(error_message, error_message_size, "invalid bytecode entry function");
        return false;
    }
    if ((bc->string_count && !bc->strings) ||
        (bc->constant_count && !bc->constants) ||
        (bc->global_count && !bc->globals) ||
        (bc->function_count && !bc->functions) ||
        (bc->code_size && !bc->code)) {
        hvm_loader_error(error_message, error_message_size, "missing bytecode section data");
        return false;
    }

    for (i = 0; i < bc->string_count; ++i) {
        if (!bc->strings[i].bytes || bc->strings[i].length > HBC_MAX_STRING_LENGTH) {
            hvm_loader_error(error_message, error_message_size, "invalid string table entry");
            return false;
        }
    }

    for (i = 0; i < bc->constant_count; ++i) {
        switch (bc->constants[i].tag) {
            case HBC_CONST_INT:
            case HBC_CONST_FLOAT:
                break;
            case HBC_CONST_STRING:
                if (bc->constants[i].as.string_index >= bc->string_count) {
                    hvm_loader_error(error_message, error_message_size, "string constant index out of range");
                    return false;
                }
                break;
            default:
                hvm_loader_error(error_message, error_message_size, "invalid constant tag");
                return false;
        }
    }

    for (i = 0; i < bc->global_count; ++i) {
        if (bc->globals[i].name_string_index >= bc->string_count) {
            hvm_loader_error(error_message, error_message_size, "global name index out of range");
            return false;
        }
    }

    for (i = 0; i < bc->function_count; ++i) {
        const HBCFunction* function = &bc->functions[i];
        size_t code_offset = function->code_offset;
        size_t code_size = function->code_size;

        if (function->name_string_index >= bc->string_count) {
            hvm_loader_error(error_message, error_message_size, "function name index out of range");
            return false;
        }
        if (function->arity > function->local_count) {
            hvm_loader_error(error_message, error_message_size, "function arity exceeds local count");
            return false;
        }
        if (code_size == 0) {
            hvm_loader_error(error_message, error_message_size, "function code is empty");
            return false;
        }
        if (code_offset > bc->code_size || code_size > bc->code_size - code_offset) {
            hvm_loader_error(error_message, error_message_size, "function code range out of bounds");
            return false;
        }
    }

    return true;
}

bool hvm_bytecode_load_file(const char* path, HBytecode* out_bytecode, char* error_message, size_t error_message_size) {
    FILE* file;
    HBCFileHeader header;
    uint32_t i;
    long file_size;
    long offset;
    size_t section_size;

    if (!path || !out_bytecode) {
        hvm_loader_error(error_message, error_message_size, "invalid loader arguments");
        return false;
    }

    hbytecode_init(out_bytecode);

    file = fopen(path, "rb");
    if (!file) {
        hvm_loader_error(error_message, error_message_size, "failed to open bytecode file");
        return false;
    }

    if (fread(&header, sizeof(header), 1, file) != 1) {
        fclose(file);
        hvm_loader_error(error_message, error_message_size, "failed to read bytecode header");
        return false;
    }

    if (memcmp(header.magic, HBC_MAGIC, 4) != 0) {
        fclose(file);
        hvm_loader_error(error_message, error_message_size, "invalid bytecode magic");
        return false;
    }

    if (header.version_major != HBC_VERSION_MAJOR || header.version_minor != HBC_VERSION_MINOR) {
        fclose(file);
        hvm_loader_error(error_message, error_message_size, "unsupported bytecode version");
        return false;
    }
    if (header.flags != 0) {
        fclose(file);
        hvm_loader_error(error_message, error_message_size, "unsupported bytecode flags");
        return false;
    }
    if (header.string_count > HBC_MAX_SECTION_COUNT ||
        header.constant_count > HBC_MAX_SECTION_COUNT ||
        header.global_count > HBC_MAX_SECTION_COUNT ||
        header.function_count > HBC_MAX_SECTION_COUNT ||
        header.code_size > HBC_MAX_CODE_SIZE) {
        fclose(file);
        hvm_loader_error(error_message, error_message_size, "bytecode section limit exceeded");
        return false;
    }
    if (header.function_count == 0 || header.entry_function_index >= header.function_count) {
        fclose(file);
        hvm_loader_error(error_message, error_message_size, "invalid bytecode entry function");
        return false;
    }

    if (fseek(file, 0, SEEK_END) != 0) {
        fclose(file);
        hvm_loader_error(error_message, error_message_size, "failed to determine bytecode size");
        return false;
    }
    file_size = ftell(file);
    if (file_size < (long)sizeof(header)) {
        fclose(file);
        hvm_loader_error(error_message, error_message_size, "truncated bytecode file");
        return false;
    }
    if (fseek(file, (long)sizeof(header), SEEK_SET) != 0) {
        fclose(file);
        hvm_loader_error(error_message, error_message_size, "failed to seek bytecode sections");
        return false;
    }
    offset = (long)sizeof(header);

    out_bytecode->version_major = header.version_major;
    out_bytecode->version_minor = header.version_minor;
    out_bytecode->entry_function_index = header.entry_function_index;
    out_bytecode->string_count = header.string_count;
    out_bytecode->constant_count = header.constant_count;
    out_bytecode->global_count = header.global_count;
    out_bytecode->function_count = header.function_count;
    out_bytecode->code_size = header.code_size;

    out_bytecode->strings = (HBCString*)calloc(out_bytecode->string_count, sizeof(HBCString));
    out_bytecode->constants = (HBCConstant*)calloc(out_bytecode->constant_count, sizeof(HBCConstant));
    out_bytecode->globals = (HBCGlobalSymbol*)calloc(out_bytecode->global_count, sizeof(HBCGlobalSymbol));
    out_bytecode->functions = (HBCFunction*)calloc(out_bytecode->function_count, sizeof(HBCFunction));
    out_bytecode->code = (uint8_t*)calloc(out_bytecode->code_size, sizeof(uint8_t));

    if ((out_bytecode->string_count && !out_bytecode->strings) ||
        (out_bytecode->constant_count && !out_bytecode->constants) ||
        (out_bytecode->global_count && !out_bytecode->globals) ||
        (out_bytecode->function_count && !out_bytecode->functions) ||
        (out_bytecode->code_size && !out_bytecode->code)) {
        fclose(file);
        hbytecode_free(out_bytecode);
        hvm_loader_error(error_message, error_message_size, "failed to allocate bytecode sections");
        return false;
    }

    for (i = 0; i < header.string_count; ++i) {
        if (!hvm_file_has_remaining(file_size, offset, sizeof(uint32_t)) ||
            !hvm_read_section(file, &out_bytecode->strings[i].length, sizeof(uint32_t), &offset)) {
            fclose(file);
            hbytecode_free(out_bytecode);
            hvm_loader_error(error_message, error_message_size, "failed to read string length");
            return false;
        }
        if (out_bytecode->strings[i].length > HBC_MAX_STRING_LENGTH ||
            !hvm_file_has_remaining(file_size, offset, out_bytecode->strings[i].length)) {
            fclose(file);
            hbytecode_free(out_bytecode);
            hvm_loader_error(error_message, error_message_size, "invalid string length");
            return false;
        }
        out_bytecode->strings[i].bytes = (char*)calloc(out_bytecode->strings[i].length + 1, sizeof(char));
        if (!out_bytecode->strings[i].bytes) {
            fclose(file);
            hbytecode_free(out_bytecode);
            hvm_loader_error(error_message, error_message_size, "failed to allocate string data");
            return false;
        }
        if (!hvm_read_section(file, out_bytecode->strings[i].bytes, out_bytecode->strings[i].length, &offset)) {
            fclose(file);
            hbytecode_free(out_bytecode);
            hvm_loader_error(error_message, error_message_size, "failed to read string data");
            return false;
        }
    }

    if (!hvm_checked_section_size((uint32_t)out_bytecode->constant_count, sizeof(HBCConstant), &section_size) ||
        !hvm_file_has_remaining(file_size, offset, section_size) ||
        !hvm_read_section(file, out_bytecode->constants, section_size, &offset)) {
        fclose(file);
        hbytecode_free(out_bytecode);
        hvm_loader_error(error_message, error_message_size, "failed to read constant pool");
        return false;
    }

    if (!hvm_checked_section_size((uint32_t)out_bytecode->global_count, sizeof(HBCGlobalSymbol), &section_size) ||
        !hvm_file_has_remaining(file_size, offset, section_size) ||
        !hvm_read_section(file, out_bytecode->globals, section_size, &offset)) {
        fclose(file);
        hbytecode_free(out_bytecode);
        hvm_loader_error(error_message, error_message_size, "failed to read globals");
        return false;
    }

    if (!hvm_checked_section_size((uint32_t)out_bytecode->function_count, sizeof(HBCFunction), &section_size) ||
        !hvm_file_has_remaining(file_size, offset, section_size) ||
        !hvm_read_section(file, out_bytecode->functions, section_size, &offset)) {
        fclose(file);
        hbytecode_free(out_bytecode);
        hvm_loader_error(error_message, error_message_size, "failed to read function table");
        return false;
    }

    if (!hvm_file_has_remaining(file_size, offset, out_bytecode->code_size) ||
        !hvm_read_section(file, out_bytecode->code, out_bytecode->code_size, &offset)) {
        fclose(file);
        hbytecode_free(out_bytecode);
        hvm_loader_error(error_message, error_message_size, "failed to read code section");
        return false;
    }

    fclose(file);
    if (!hvm_validate_loaded_bytecode(out_bytecode, error_message, error_message_size)) {
        hbytecode_free(out_bytecode);
        return false;
    }
    return true;
}
