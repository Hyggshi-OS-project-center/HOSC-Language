/* test_compiler.c - HOSC source file */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "token.h"
#include "lexer.h"
#include "parser.h"
#include "codegen.h"
#include "ast.h"
#include "hvm.h"
#include "hvm_compiler.h"
#include "assert_helpers.h"

static ASTNode* first_decl(ASTNode* program) {
    if (!program || program->type != AST_PROGRAM) return NULL;
    if (!program->data.program.declarations) return NULL;
    return program->data.program.declarations->node;
}

static void free_program(ASTNode *program) {
    free_ast(program);
    ast_release_arena();
}

static ASTNode* find_function(ASTNode *program, const char *name) {
    ASTNodeList *cur;
    if (!program || program->type != AST_PROGRAM) return NULL;
    for (cur = program->data.program.declarations; cur; cur = cur->next) {
        ASTNode *n = cur->node;
        if (n && n->type == AST_FUNCTION && n->data.function.name && strcmp(n->data.function.name, name) == 0) {
            return n;
        }
    }
    return NULL;
}

static ASTNode* block_stmt(ASTNode *block, int index) {
    ASTNodeList *cur;
    int i = 0;
    if (!block || block->type != AST_BLOCK) return NULL;
    for (cur = block->data.block.statements; cur; cur = cur->next, i++) {
        if (i == index) return cur->node;
    }
    return NULL;
}

static int has_opcode(HVM_VM *vm, HVM_Opcode op) {
    size_t i;
    if (!vm) return 0;
    for (i = 0; i < vm->instruction_count; i++) {
        if (vm->instructions[i].opcode == op) return 1;
    }
    return 0;
}

static void test_lexer(void) {
    const char *source = "let x = 10;";
    Token *t = lexer_tokenize(source);
    ASSERT_NOT_NULL(t);
    ASSERT_EQ_INT(t[0].type, TOKEN_LET);
    ASSERT_EQ_INT(t[1].type, TOKEN_IDENTIFIER);
    ASSERT_STR_EQ(t[1].value.identifier, "x");
    ASSERT_EQ_INT(t[2].type, TOKEN_ASSIGN);
    ASSERT_EQ_INT(t[3].type, TOKEN_NUMBER);
    ASSERT_EQ_INT(t[3].value.number, 10);
    ASSERT_EQ_INT(t[4].type, TOKEN_SEMICOLON);
    ASSERT_EQ_INT(t[5].type, TOKEN_EOF);
    free_tokens(t);
}

static void test_parser_let(void) {
    const char *source = "let x = 10;";
    ASTNode *prog = parser_parse(source);
    ASTNode *decl;
    ASSERT_NOT_NULL(prog);
    ASSERT_EQ_INT(prog->type, AST_PROGRAM);
    decl = first_decl(prog);
    ASSERT_NOT_NULL(decl);
    ASSERT_EQ_INT(decl->type, AST_VARIABLE_DECLARATION);
    ASSERT_STR_EQ(decl->data.variable_declaration.identifier, "x");
    ASSERT_NOT_NULL(decl->data.variable_declaration.value);
    ASSERT_EQ_INT(decl->data.variable_declaration.value->type, AST_NUMBER);
    ASSERT_EQ_INT(decl->data.variable_declaration.value->data.number.value, 10);
    free_program(prog);
}

static void test_parser_binary(void) {
    const char *source = "let x = 10 + 20;";
    ASTNode *prog = parser_parse(source);
    ASTNode *decl = first_decl(prog);
    ASTNode *value;
    ASSERT_NOT_NULL(decl);
    value = decl->data.variable_declaration.value;
    ASSERT_EQ_INT(value->type, AST_BINARY_OP);
    ASSERT_EQ_INT(value->data.binary_op.left->type, AST_NUMBER);
    ASSERT_EQ_INT(value->data.binary_op.right->type, AST_NUMBER);
    ASSERT_EQ_INT(value->data.binary_op.op, TOKEN_PLUS);
    free_program(prog);
}

static void test_parser_print(void) {
    const char *source = "print(1 + 2);";
    ASTNode *prog = parser_parse(source);
    ASTNode *decl = first_decl(prog);
    ASSERT_NOT_NULL(decl);
    ASSERT_EQ_INT(decl->type, AST_PRINT_STATEMENT);
    ASSERT_EQ_INT(decl->data.print_statement.expression->type, AST_BINARY_OP);
    free_program(prog);
}

static void test_parser_gui(void) {
    const char *source =
        "package main\n"
        "func main() {\n"
        "  window(\"HOSC Window\")\n"
        "  text(100, 120, \"Hello GUI\")\n"
        "}\n";
    ASTNode *prog = parser_parse(source);
    ASTNode *main_fn;
    ASTNode *stmt0;
    ASTNode *stmt1;

    ASSERT_NOT_NULL(prog);
    main_fn = find_function(prog, "main");
    ASSERT_NOT_NULL(main_fn);
    ASSERT_EQ_INT(main_fn->type, AST_FUNCTION);

    stmt0 = block_stmt(main_fn->data.function.body, 0);
    stmt1 = block_stmt(main_fn->data.function.body, 1);

    ASSERT_NOT_NULL(stmt0);
    ASSERT_NOT_NULL(stmt1);
    ASSERT_EQ_INT(stmt0->type, AST_WINDOW_STMT);
    ASSERT_EQ_INT(stmt1->type, AST_TEXT_STMT);
    ASSERT_STR_EQ(stmt0->data.window_stmt.title, "HOSC Window");
    ASSERT_STR_EQ(stmt1->data.text_stmt.msg, "Hello GUI");

    free_program(prog);
}

static void test_codegen_statement(void) {
    const char *source = "let x = 10;";
    ASTNode *prog = parser_parse(source);
    ASTNode *decl = first_decl(prog);
    char *out = codegen_generate(decl);
    ASSERT_NOT_NULL(out);
    ASSERT_STR_EQ(out, "int x = 10;\n");
    free(out);
    free_program(prog);
}

static void test_codegen_function_program(void) {
    const char *source = "package main func main() { let x = 10 + 20; print(x); }";
    ASTNode *prog = parser_parse(source);
    char *out = codegen_generate(prog);
    ASSERT_NOT_NULL(out);
    ASSERT_NOT_NULL(strstr(out, "int main()"));
    free(out);
    free_program(prog);
}

static void test_hvm_gui_pipeline(void) {
    const char *source =
        "package main\n"
        "func main() {\n"
        "  window(\"GUI Test\")\n"
        "  var i = 0\n"
        "  while i < 2 {\n"
        "    text(10, i, \"tick\")\n"
        "    i = i + 1\n"
        "  }\n"
        "}\n";
    ASTNode *prog = parser_parse(source);
    HVM_VM *vm;
    HVM_Compiler *compiler;

    ASSERT_NOT_NULL(prog);

    vm = hvm_create();
    ASSERT_NOT_NULL(vm);

    compiler = hvm_compiler_create(vm);
    ASSERT_NOT_NULL(compiler);

    ASSERT_EQ_INT(hvm_compiler_compile_ast(compiler, prog), 1);
    ASSERT_EQ_INT(has_opcode(vm, HVM_CREATE_WINDOW), 1);
    ASSERT_EQ_INT(has_opcode(vm, HVM_DRAW_TEXT), 1);
    ASSERT_EQ_INT(has_opcode(vm, HVM_JUMP_IF_FALSE), 1);
    ASSERT_EQ_INT(has_opcode(vm, HVM_JUMP), 1);
    ASSERT_EQ_INT(has_opcode(vm, HVM_CALL), 1);
    ASSERT_EQ_INT(has_opcode(vm, HVM_RETURN), 1);

#ifdef _WIN32
    _putenv("HVM_GUI_HEADLESS=1");
#endif
    ASSERT_EQ_INT(hvm_run(vm), 1);
#ifdef _WIN32
    _putenv("HVM_GUI_HEADLESS=");
#endif

    hvm_compiler_destroy(compiler);
    hvm_destroy(vm);
    free_program(prog);
}

int main(void) {
    printf("[TEST] lexer...\n");
    test_lexer();

    printf("[TEST] parser let...\n");
    test_parser_let();

    printf("[TEST] parser binary...\n");
    test_parser_binary();

    printf("[TEST] parser print...\n");
    test_parser_print();

    printf("[TEST] parser gui...\n");
    test_parser_gui();

    printf("[TEST] codegen statement...\n");
    test_codegen_statement();

    printf("[TEST] codegen program...\n");
    test_codegen_function_program();

    printf("[TEST] hvm gui pipeline...\n");
    test_hvm_gui_pipeline();

    printf("All tests passed.\n");
    return 0;
}


