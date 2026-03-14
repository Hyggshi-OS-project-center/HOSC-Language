/* bytecode.cpp - C++ bytecode container for HVM */

#include <deque>
#include <new>
#include <string>
#include <vector>

#include "vm_bytecode.h"

struct HVM_Bytecode {
    std::vector<HVM_Instruction> code;
    std::deque<std::string> strings;
};

static int opcode_uses_string_operand(HVM_Opcode opcode) {
    return opcode == HVM_PUSH_STRING ||
           opcode == HVM_STORE_GLOBAL ||
           opcode == HVM_LOAD_GLOBAL ||
           opcode == HVM_CREATE_WINDOW ||
           opcode == HVM_CALL_NATIVE;
}

static void assign_string_operand(HVM_Bytecode* bytecode, HVM_Instruction* instr, const char* operand) {
    if (!bytecode || !instr) return;
    bytecode->strings.emplace_back(operand ? operand : "");
    instr->operand.string_operand = const_cast<char*>(bytecode->strings.back().c_str());
}

extern "C" {

HVM_Bytecode* hvm_bytecode_create(void) {
    return new (std::nothrow) HVM_Bytecode();
}

void hvm_bytecode_destroy(HVM_Bytecode* bytecode) {
    delete bytecode;
}

void hvm_bytecode_clear(HVM_Bytecode* bytecode) {
    if (!bytecode) return;
    bytecode->code.clear();
    bytecode->strings.clear();
}

size_t hvm_bytecode_count(const HVM_Bytecode* bytecode) {
    return bytecode ? bytecode->code.size() : 0;
}

size_t hvm_bytecode_capacity(const HVM_Bytecode* bytecode) {
    return bytecode ? bytecode->code.capacity() : 0;
}

HVM_Instruction* hvm_bytecode_data(HVM_Bytecode* bytecode) {
    return bytecode && !bytecode->code.empty() ? bytecode->code.data() : NULL;
}

const HVM_Instruction* hvm_bytecode_cdata(const HVM_Bytecode* bytecode) {
    return bytecode && !bytecode->code.empty() ? bytecode->code.data() : NULL;
}

int hvm_bytecode_add_int(HVM_Bytecode* bytecode, HVM_Opcode opcode, int64_t operand) {
    if (!bytecode) return 0;
    HVM_Instruction instr = {};
    instr.opcode = opcode;
    instr.operand.int_operand = operand;
    bytecode->code.push_back(instr);
    return 1;
}

int hvm_bytecode_add_float(HVM_Bytecode* bytecode, HVM_Opcode opcode, double operand) {
    if (!bytecode) return 0;
    HVM_Instruction instr = {};
    instr.opcode = opcode;
    instr.operand.float_operand = operand;
    bytecode->code.push_back(instr);
    return 1;
}

int hvm_bytecode_add_string(HVM_Bytecode* bytecode, HVM_Opcode opcode, const char* operand) {
    if (!bytecode) return 0;
    HVM_Instruction instr = {};
    instr.opcode = opcode;
    if (opcode_uses_string_operand(opcode)) {
        assign_string_operand(bytecode, &instr, operand);
    } else {
        instr.operand.string_operand = const_cast<char*>(operand ? operand : "");
    }
    bytecode->code.push_back(instr);
    return 1;
}

int hvm_bytecode_add_address(HVM_Bytecode* bytecode, HVM_Opcode opcode, size_t address) {
    if (!bytecode) return 0;
    HVM_Instruction instr = {};
    instr.opcode = opcode;
    instr.operand.address_operand = address;
    bytecode->code.push_back(instr);
    return 1;
}

int hvm_bytecode_load(HVM_Bytecode* bytecode, const HVM_Instruction* instructions, size_t count) {
    size_t i;
    if (!bytecode || (!instructions && count > 0)) return 0;
    bytecode->code.clear();
    bytecode->strings.clear();
    bytecode->code.reserve(count);
    for (i = 0; i < count; i++) {
        HVM_Instruction instr = instructions[i];
        if (opcode_uses_string_operand(instr.opcode)) {
            const char* operand = instr.operand.string_operand ? instr.operand.string_operand : "";
            assign_string_operand(bytecode, &instr, operand);
        }
        bytecode->code.push_back(instr);
    }
    return 1;
}

} /* extern "C" */
