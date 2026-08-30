/*
 * HOSC Bootstrap Compiler & Interpreter
 * File: bootstrap/include/bs_repl.h
 * Purpose: Interactive Read-Eval-Print Loop with live command registration
 */

#ifndef HOSC_BOOTSTRAP_REPL_H
#define HOSC_BOOTSTRAP_REPL_H

#include "bs_runtime.h"

#ifdef __cplusplus
extern "C" {
#endif

void bs_repl_start(BsRuntime *runtime);

#ifdef __cplusplus
}
#endif

#endif /* HOSC_BOOTSTRAP_REPL_H */
