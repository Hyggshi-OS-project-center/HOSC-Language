/*
 * File: compiler/include/hosc_type_checker.h
 * Purpose: HOSC Type Checker and Semantic Analysis interface.
 */

#pragma once
#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>
#include "ast.h"
#include "hosc_diag.h"

bool hosc_type_check(ASTNode *ast, HDiagnosticBag *diagnostics, const char *file_path);

#ifdef __cplusplus
}
#endif
