/*
 * HOSC Bootstrap — Integration Test Suite
 * File: bootstrap/tests/test_bootstrap.c
 * Purpose: Tests for lexer, parser, command registry, AST rewriter, and interpreter
 */

#include "bootstrap.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ── Test infrastructure ────────────────────────────────── */
static int g_pass = 0;
static int g_fail = 0;

#define TEST_ASSERT(label, condition) \
    do { \
        if (condition) { \
            printf("  [PASS] %s\n", label); \
            g_pass++; \
        } else { \
            printf("  [FAIL] %s  (at %s:%d)\n", label, __FILE__, __LINE__); \
            g_fail++; \
        } \
    } while(0)

#define TEST_ASSERT_STR(label, a, b) \
    TEST_ASSERT(label, strcmp((a) ? (a) : "", (b) ? (b) : "") == 0)

static void section(const char *name) {
    printf("\n── %s ─────────────────────────────\n", name);
}

/* ── Helpers ──────────────────────────────────────────────── */
static BsRuntime *make_rt(void) {
    return bs_runtime_create();
}

/* Run source string via interpreter, return true if no error */
static bool run(BsRuntime *rt, const char *src) {
    return bs_run_source_interpreter(rt, src);
}

/* ── Lexer Tests ──────────────────────────────────────────── */
static void test_lexer(void) {
    section("Lexer");
    {
        BsLexer lex;
        bs_lexer_init(&lex, "var x = 42;");
        BsToken t;

        t = bs_lexer_next_token(&lex);
        TEST_ASSERT("lex: var keyword", t.type == BS_TOK_VAR);

        t = bs_lexer_next_token(&lex);
        TEST_ASSERT("lex: identifier 'x'", t.type == BS_TOK_IDENTIFIER);
        TEST_ASSERT_STR("lex: identifier value", t.as.str_val, "x");
        if (t.as.str_val) free(t.as.str_val);

        t = bs_lexer_next_token(&lex);
        TEST_ASSERT("lex: '=' assign", t.type == BS_TOK_ASSIGN);

        t = bs_lexer_next_token(&lex);
        TEST_ASSERT("lex: integer 42", t.type == BS_TOK_NUMBER_INT);
        TEST_ASSERT("lex: integer value 42", t.as.int_val == 42);

        t = bs_lexer_next_token(&lex);
        TEST_ASSERT("lex: semicolon", t.type == BS_TOK_SEMICOLON);

        t = bs_lexer_next_token(&lex);
        TEST_ASSERT("lex: EOF", t.type == BS_TOK_EOF);
    }
    {
        BsLexer lex;
        bs_lexer_init(&lex, "command macro syntax");
        BsToken t;
        t = bs_lexer_next_token(&lex);
        TEST_ASSERT("lex: 'command' keyword", t.type == BS_TOK_COMMAND);
        t = bs_lexer_next_token(&lex);
        TEST_ASSERT("lex: 'macro' keyword", t.type == BS_TOK_MACRO);
        t = bs_lexer_next_token(&lex);
        TEST_ASSERT("lex: 'syntax' keyword", t.type == BS_TOK_SYNTAX);
    }
    {
        BsLexer lex;
        bs_lexer_init(&lex, "\"hello world\"");
        BsToken t = bs_lexer_next_token(&lex);
        TEST_ASSERT("lex: string literal", t.type == BS_TOK_STRING);
        TEST_ASSERT_STR("lex: string value", t.as.str_val, "hello world");
        if (t.as.str_val) free(t.as.str_val);
    }
    {
        BsLexer lex;
        bs_lexer_init(&lex, "3.14");
        BsToken t = bs_lexer_next_token(&lex);
        TEST_ASSERT("lex: float literal", t.type == BS_TOK_NUMBER_FLOAT);
        TEST_ASSERT("lex: float value ~3.14", t.as.float_val > 3.13 && t.as.float_val < 3.15);
    }
}

/* ── Parser Tests ─────────────────────────────────────────── */
static void test_parser(void) {
    section("Parser");
    {
        BsParser p;
        bs_parser_init(&p, "var x = 10;");
        ASTNode *prog = bs_parser_parse_program(&p);
        TEST_ASSERT("parse: no error", !p.had_error);
        TEST_ASSERT("parse: program is block", prog && prog->type == AST_STMT_BLOCK);
        TEST_ASSERT("parse: one statement", prog->as.block.statements.count == 1);
        if (prog) {
            ASTNode *stmt = prog->as.block.statements.items[0];
            TEST_ASSERT("parse: var decl", stmt->type == AST_STMT_VAR_DECL);
            TEST_ASSERT_STR("parse: var name", stmt->as.var_decl.name, "x");
        }
        bs_ast_free(prog);
    }
    {
        BsParser p;
        bs_parser_init(&p, "func add(a, b) { return a + b; }");
        ASTNode *prog = bs_parser_parse_program(&p);
        TEST_ASSERT("parse: func decl no error", !p.had_error);
        TEST_ASSERT("parse: func decl node", prog && prog->as.block.statements.count == 1);
        if (prog) {
            ASTNode *fn = prog->as.block.statements.items[0];
            TEST_ASSERT("parse: func decl type", fn->type == AST_STMT_FUNC_DECL);
            TEST_ASSERT_STR("parse: func name", fn->as.func_decl.name, "add");
            TEST_ASSERT("parse: func param count", fn->as.func_decl.param_count == 2);
        }
        bs_ast_free(prog);
    }
    {
        BsParser p;
        bs_parser_init(&p, "command log(msg) { print(msg); }");
        ASTNode *prog = bs_parser_parse_program(&p);
        TEST_ASSERT("parse: command def no error", !p.had_error);
        if (prog && prog->as.block.statements.count > 0) {
            ASTNode *cmd = prog->as.block.statements.items[0];
            TEST_ASSERT("parse: command def type", cmd->type == AST_STMT_COMMAND_DEF);
            TEST_ASSERT_STR("parse: command name", cmd->as.extension_def.name, "log");
        }
        bs_ast_free(prog);
    }
    {
        BsParser p;
        bs_parser_init(&p, "macro unless(cond) { if (!cond) { print(\"ok\"); } }");
        ASTNode *prog = bs_parser_parse_program(&p);
        TEST_ASSERT("parse: macro def no error", !p.had_error);
        if (prog && prog->as.block.statements.count > 0) {
            ASTNode *mac = prog->as.block.statements.items[0];
            TEST_ASSERT("parse: macro def type", mac->type == AST_STMT_MACRO_DEF);
            TEST_ASSERT_STR("parse: macro name", mac->as.extension_def.name, "unless");
        }
        bs_ast_free(prog);
    }
}

/* ── Command Registry Tests ───────────────────────────────── */
static BsValue dummy_native(BsRuntime *rt, BsValue *args, size_t argc) {
    (void)rt; (void)args; (void)argc;
    return bs_int(42);
}

static void test_command_registry(void) {
    section("Command Registry");

    BsCommandRegistry reg;
    bs_command_registry_init(&reg);

    TEST_ASSERT("registry: empty initially", reg.count == 0);
    TEST_ASSERT("registry: lookup unknown = NULL", bs_command_registry_lookup(&reg, "foo") == NULL);

    bool ok = bs_register_native_command(&reg, "myCmd", 1, dummy_native);
    TEST_ASSERT("registry: register native ok", ok);
    TEST_ASSERT("registry: count == 1", reg.count == 1);

    BsCommand *cmd = bs_command_registry_lookup(&reg, "myCmd");
    TEST_ASSERT("registry: lookup returns command", cmd != NULL);
    if (cmd) {
        TEST_ASSERT("registry: kind is NATIVE", cmd->kind == COMMAND_NATIVE);
        TEST_ASSERT("registry: param count", cmd->parameter_count == 1);
        TEST_ASSERT("registry: native ptr set", cmd->native == dummy_native);
    }

    /* Re-register same name should replace */
    bs_register_native_command(&reg, "myCmd", 2, dummy_native);
    TEST_ASSERT("registry: count still 1 after re-register", reg.count == 1);
    cmd = bs_command_registry_lookup(&reg, "myCmd");
    TEST_ASSERT("registry: re-register updates param count", cmd && cmd->parameter_count == 2);

    bs_command_registry_free(&reg);
    TEST_ASSERT("registry: free resets count", reg.count == 0);
}

/* ── Interpreter / Run Tests ──────────────────────────────── */
static void test_interpreter(void) {
    section("Interpreter");

    {
        BsRuntime *rt = make_rt();
        bool ok = run(rt, "var x = 5 + 3; print(x);");
        TEST_ASSERT("interp: simple arithmetic", ok);
        bs_runtime_free(rt);
    }

    {
        BsRuntime *rt = make_rt();
        bool ok = run(rt, "func sq(n) { return n * n; } var r = sq(7); print(r);");
        TEST_ASSERT("interp: function call", ok);

        BsValue v;
        bool got = bs_env_get(rt->global_env, "r", &v);
        TEST_ASSERT("interp: func result stored", got);
        if (got) {
            TEST_ASSERT("interp: func result value", v.type == BS_VAL_INT && v.as.integer == 49);
            bs_free_value(v);
        }
        bs_runtime_free(rt);
    }

    {
        BsRuntime *rt = make_rt();
        bool ok = run(rt,
            "command greet(name) { print(\"Hi \" + name); }\n"
            "greet(\"HOSC\");"
        );
        TEST_ASSERT("interp: AST command", ok);
        bs_runtime_free(rt);
    }

    {
        BsRuntime *rt = make_rt();
        bool ok = run(rt,
            "var x = 0;\n"
            "while (x < 5) { x = x + 1; }\n"
        );
        TEST_ASSERT("interp: while loop runs", ok);
        BsValue v;
        bool got = bs_env_get(rt->global_env, "x", &v);
        TEST_ASSERT("interp: while result x == 5", got && v.as.integer == 5);
        if (got) bs_free_value(v);
        bs_runtime_free(rt);
    }

    {
        BsRuntime *rt = make_rt();
        bool ok = run(rt,
            "var s = \"hello\" + \" \" + \"world\";"
        );
        TEST_ASSERT("interp: string concat", ok);
        BsValue v;
        bool got = bs_env_get(rt->global_env, "s", &v);
        TEST_ASSERT("interp: string result", got && v.type == BS_VAL_STRING);
        if (got) {
            TEST_ASSERT_STR("interp: string value", v.as.string, "hello world");
            bs_free_value(v);
        }
        bs_runtime_free(rt);
    }
}

/* ── C API Tests ──────────────────────────────────────────── */
static BsValue capi_double_cmd(BsRuntime *rt, BsValue *args, size_t argc) {
    (void)rt;
    if (argc < 1 || args[0].type != BS_VAL_INT) return bs_null();
    return bs_int(args[0].as.integer * 2);
}

static void test_c_api(void) {
    section("C API — bootstrap_register_command");

    BsRuntime *rt = make_rt();

    bool ok = bootstrap_register_command(rt, "double_it", 1, capi_double_cmd);
    TEST_ASSERT("c_api: registration succeeds", ok);
    TEST_ASSERT("c_api: command in registry", bs_command_registry_has(&rt->command_registry, "double_it"));

    bool run_ok = run(rt, "var result = double_it(21);");
    TEST_ASSERT("c_api: run with custom command", run_ok);

    BsValue v;
    bool got = bs_env_get(rt->global_env, "result", &v);
    TEST_ASSERT("c_api: result present", got);
    if (got) {
        TEST_ASSERT("c_api: result value == 42", v.type == BS_VAL_INT && v.as.integer == 42);
        bs_free_value(v);
    }

    bs_runtime_free(rt);
}

/* ── Summary ──────────────────────────────────────────────── */
static void print_summary(void) {
    printf("\n══════════════════════════════════════\n");
    printf("Bootstrap Test Results\n");
    printf("  Passed: %d\n", g_pass);
    printf("  Failed: %d\n", g_fail);
    printf("  Total:  %d\n", g_pass + g_fail);
    if (g_fail == 0) {
        printf("  Status: ALL TESTS PASSED ✓\n");
    } else {
        printf("  Status: %d TEST(S) FAILED ✗\n", g_fail);
    }
    printf("══════════════════════════════════════\n");
}

int main(void) {
    printf("HOSC Bootstrap Test Suite\n");

    test_lexer();
    test_parser();
    test_command_registry();
    test_interpreter();
    test_c_api();

    print_summary();
    return g_fail > 0 ? 1 : 0;
}
