/*
 * HOSC Bootstrap Compiler & Interpreter
 * File: bootstrap/include/bootstrap.h
 * Purpose: Master include header exposing the complete bootstrap toolchain & C API
 */

#ifndef HOSC_BOOTSTRAP_H
#define HOSC_BOOTSTRAP_H

#include "bs_value.h"
#include "bs_token.h"
#include "bs_lexer.h"
#include "bs_ast.h"
#include "bs_command_registry.h"
#include "bs_ast_rewriter.h"
#include "bs_parser.h"
#include "bs_runtime.h"
#include "bs_interpreter.h"
#include "bs_bytecode.h"
#include "bs_codegen.h"
#include "bs_vm.h"
#include "bs_repl.h"

#ifdef __cplusplus
extern "C" {
#endif

/* High-level execution APIs */
bool bs_run_source_interpreter(BsRuntime *runtime, const char *source);
bool bs_run_source_bytecode(BsRuntime *runtime, const char *source);
bool bs_run_file(BsRuntime *runtime, const char *filepath, bool use_vm);

#ifdef __cplusplus
}
#endif

#endif /* HOSC_BOOTSTRAP_H */
