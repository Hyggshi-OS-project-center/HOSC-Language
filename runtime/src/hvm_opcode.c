/*
 * File: runtime\src\hvm_opcode.c
 * Purpose: HOSC source file.
 */

static int opcode_uses_string_operand(HVM_Opcode opcode) {
    return opcode == HVM_PUSH_STRING ||
           opcode == HVM_STORE_GLOBAL ||
           opcode == HVM_LOAD_GLOBAL ||
           opcode == HVM_CREATE_WINDOW;
}

static void hvm_clear_instructions(HVM_VM* vm) {
    size_t i;
    if (!vm || !vm->instructions) return;
    for (i = 0; i < vm->instruction_count; i++) {
        if (opcode_uses_string_operand(vm->instructions[i].opcode) && vm->instructions[i].operand.string_operand) {
            free(vm->instructions[i].operand.string_operand);
            vm->instructions[i].operand.string_operand = NULL;
        }
    }
    free(vm->instructions);
    vm->instructions = NULL;
    vm->instruction_count = 0;
    vm->instruction_capacity = 0;
    vm->pc = 0;
}

int hvm_load_bytecode(HVM_VM* vm, const HVM_Instruction* instructions, size_t count) {
    size_t i;
    if (!vm || (!instructions && count > 0)) return 0;
    if (count > HVM_MAX_INSTRUCTIONS) return 0;

    hvm_clear_instructions(vm);

    if (count == 0) {
        vm->pc = 0;
        return 1;
    }

    vm->instructions = (HVM_Instruction*)calloc(count, sizeof(HVM_Instruction));
    if (!vm->instructions) return 0;

    for (i = 0; i < count; i++) {
        vm->instructions[i].opcode = instructions[i].opcode;
        if (opcode_uses_string_operand(instructions[i].opcode)) {
            if (instructions[i].operand.string_operand) {
                vm->instructions[i].operand.string_operand = strdup(instructions[i].operand.string_operand);
                if (!vm->instructions[i].operand.string_operand) {
                    hvm_clear_instructions(vm);
                    return 0;
                }
            } else {
                vm->instructions[i].operand.string_operand = NULL;
            }
        } else {
            vm->instructions[i].operand = instructions[i].operand;
        }
    }

    vm->instruction_count = count;
    vm->instruction_capacity = count;
    vm->pc = 0;
    return 1;
}

static int ensure_instruction_capacity(HVM_VM* vm) {
    size_t new_cap;
    HVM_Instruction* ni;

    if (vm->instruction_count < vm->instruction_capacity) return 1;
    if (vm->instruction_capacity >= HVM_MAX_INSTRUCTIONS) return 0;

    new_cap = vm->instruction_capacity ? vm->instruction_capacity * 2 : 16;
    if (new_cap > HVM_MAX_INSTRUCTIONS) new_cap = HVM_MAX_INSTRUCTIONS;

    ni = (HVM_Instruction*)realloc(vm->instructions, sizeof(HVM_Instruction) * new_cap);
    if (!ni) return 0;

    memset(ni + vm->instruction_capacity, 0, sizeof(HVM_Instruction) * (new_cap - vm->instruction_capacity));
    vm->instructions = ni;
    vm->instruction_capacity = new_cap;
    return 1;
}

int hvm_add_instruction(HVM_VM* vm, HVM_Opcode opcode, int64_t operand) {
    if (!vm || !ensure_instruction_capacity(vm)) return 0;
    vm->instructions[vm->instruction_count].opcode = opcode;
    vm->instructions[vm->instruction_count].operand.int_operand = operand;
    vm->instruction_count++;
    return 1;
}

int hvm_add_instruction_float(HVM_VM* vm, HVM_Opcode opcode, double operand) {
    if (!vm || !ensure_instruction_capacity(vm)) return 0;
    vm->instructions[vm->instruction_count].opcode = opcode;
    vm->instructions[vm->instruction_count].operand.float_operand = operand;
    vm->instruction_count++;
    return 1;
}

int hvm_add_instruction_string(HVM_VM* vm, HVM_Opcode opcode, const char* operand) {
    if (!vm || !operand || !ensure_instruction_capacity(vm)) return 0;
    vm->instructions[vm->instruction_count].opcode = opcode;
    vm->instructions[vm->instruction_count].operand.string_operand = strdup(operand);
    if (!vm->instructions[vm->instruction_count].operand.string_operand) return 0;
    vm->instruction_count++;
    return 1;
}

int hvm_add_instruction_address(HVM_VM* vm, HVM_Opcode opcode, size_t address) {
    if (!vm || !ensure_instruction_capacity(vm)) return 0;
    vm->instructions[vm->instruction_count].opcode = opcode;
    vm->instructions[vm->instruction_count].operand.address_operand = address;
    vm->instruction_count++;
    return 1;
}
