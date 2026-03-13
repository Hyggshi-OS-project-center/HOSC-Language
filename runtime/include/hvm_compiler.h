/*
 * File: runtime\include\hvm_compiler.h
 * Purpose: HOSC source file.
 */

// hvm_compiler.h - HOSC Virtual Machine Compiler Header

#ifndef HVM_COMPILER_H
#define HVM_COMPILER_H

#ifdef __cplusplus
extern "C" {
#endif

#include "ast.h"
#include "hvm.h"

typedef struct {
    HVM_VM* vm;
    size_t current_instruction;
    int error_count;
    char** error_messages;
    void* internal;
} HVM_Compiler;

HVM_Compiler* hvm_compiler_create(HVM_VM* vm);
void hvm_compiler_destroy(HVM_Compiler* compiler);
int hvm_compiler_compile_ast(HVM_Compiler* compiler, ASTNode* ast);

int hvm_compile_program(HVM_Compiler* compiler, ASTNode* ast);
int hvm_compile_statement(HVM_Compiler* compiler, ASTNode* ast);
int hvm_compile_expression(HVM_Compiler* compiler, ASTNode* ast);
int hvm_compile_variable_declaration(HVM_Compiler* compiler, ASTNode* ast);
int hvm_compile_number_literal(HVM_Compiler* compiler, ASTNode* ast);

int hvm_compile_print_statement(HVM_Compiler* compiler, ASTNode* ast);
int hvm_compile_win32_message_box(HVM_Compiler* compiler, ASTNode* ast);
int hvm_compile_win32_error(HVM_Compiler* compiler, ASTNode* ast);
int hvm_compile_win32_info(HVM_Compiler* compiler, ASTNode* ast);
int hvm_compile_win32_warning(HVM_Compiler* compiler, ASTNode* ast);
int hvm_compile_sleep(HVM_Compiler* compiler, ASTNode* ast);
int hvm_compile_beep(HVM_Compiler* compiler, ASTNode* ast);

void hvm_compiler_add_error(HVM_Compiler* compiler, const char* message);
int hvm_compiler_has_errors(HVM_Compiler* compiler);
void hvm_compiler_print_errors(HVM_Compiler* compiler);

int hvm_compiler_add_instruction(HVM_Compiler* compiler, HVM_Opcode opcode, int64_t operand);
int hvm_compiler_add_instruction_float(HVM_Compiler* compiler, HVM_Opcode opcode, double operand);
int hvm_compiler_add_instruction_string(HVM_Compiler* compiler, HVM_Opcode opcode, const char* operand);

#ifdef __cplusplus
}
#endif

#endif // HVM_COMPILER_H
