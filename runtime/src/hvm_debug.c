/*
 * File: runtime\src\hvm_debug.c
 * Purpose: HOSC source file.
 */

void hvm_print_instructions(HVM_VM* vm) {
    size_t i;
    if (!vm || !vm->instructions) return;
    printf("Instructions:\n");
    for (i = 0; i < vm->instruction_count; i++) {
        HVM_Instruction* instr = &vm->instructions[i];
        printf("  [%zu]: opcode=%d\n", i, instr->opcode);
    }
}

