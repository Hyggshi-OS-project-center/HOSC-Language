/*
 * HOSC Bootstrap Compiler & Interpreter
 * File: bootstrap/src/bs_repl.c
 * Purpose: Interactive REPL with live command registration
 */

#include "bs_repl.h"
#include "bs_parser.h"
#include "bs_ast_rewriter.h"
#include "bs_interpreter.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define REPL_INPUT_MAX 4096

void bs_repl_start(BsRuntime *runtime) {
    printf("HOSC Bootstrap REPL v0.1 (Compiler 0)\n");
    printf("Extensible Interpreter — type 'exit' or Ctrl+C to quit.\n");
    printf("Use 'command name(args) { ... }' or 'macro name(args) { ... }' to extend the language.\n");
    printf("\n");

    char input[REPL_INPUT_MAX];
    BsAstRewriter rewriter;
    bs_ast_rewriter_init(&rewriter, &runtime->command_registry);

    while (1) {
        printf("hosc> ");
        fflush(stdout);

        if (!fgets(input, sizeof(input), stdin)) {
            printf("\n");
            break;
        }

        /* Strip trailing newline */
        size_t len = strlen(input);
        while (len > 0 && (input[len - 1] == '\n' || input[len - 1] == '\r')) {
            input[--len] = '\0';
        }

        if (len == 0) continue;
        if (strcmp(input, "exit") == 0 || strcmp(input, "quit") == 0) break;

        /* Special REPL commands */
        if (strcmp(input, ":cmds") == 0) {
            printf("Registered commands (%zu):\n", runtime->command_registry.count);
            for (size_t i = 0; i < runtime->command_registry.count; i++) {
                BsCommand *cmd = runtime->command_registry.commands[i];
                const char *kind = cmd->kind == COMMAND_NATIVE ? "native" :
                                   cmd->kind == COMMAND_AST    ? "command" : "macro";
                printf("  %-20s (%s, %zu args)\n", cmd->name, kind, cmd->parameter_count);
            }
            continue;
        }

        if (strcmp(input, ":help") == 0) {
            printf("Commands:\n");
            printf("  :cmds        — list all registered commands and macros\n");
            printf("  :help        — show this help\n");
            printf("  exit / quit  — exit REPL\n");
            printf("\nLanguage constructs:\n");
            printf("  var x = <expr>;              — declare variable\n");
            printf("  const x = <expr>;            — declare constant\n");
            printf("  func f(a, b) { ... }         — declare function\n");
            printf("  command log(msg) { ... }     — register behavioral command\n");
            printf("  macro unless(cond) { ... }   — register syntax macro\n");
            printf("  syntax unless(cond) { ... }  — alias for macro\n");
            continue;
        }

        /* Parse */
        BsParser parser;
        bs_parser_init(&parser, input);
        ASTNode *program = bs_parser_parse_program(&parser);

        if (parser.had_error) {
            fprintf(stderr, "  Parse error: %s\n", parser.error_message);
            bs_ast_free(program);
            continue;
        }

        /* AST Rewrite (macro expansion, command registration) */
        ASTNode *rewritten = bs_ast_rewrite(&rewriter, program);

        /* Evaluate */
        BsEvalResult res = bs_eval_program(runtime, rewritten);
        bs_ast_free(rewritten);

        if (res.status == BS_EVAL_ERROR) {
            fprintf(stderr, "  Runtime error: %s\n", runtime->error_message);
            runtime->runtime_error = false;
            runtime->error_message[0] = '\0';
        } else if (res.status == BS_EVAL_OK && res.value.type != BS_VAL_NULL) {
            printf("  => ");
            bs_print_value(res.value);
            printf("\n");
        }
        bs_free_value(res.value);
    }

    printf("Goodbye.\n");
}
