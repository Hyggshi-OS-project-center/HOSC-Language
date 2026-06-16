#ifndef HOSC_CLI_H
#define HOSC_CLI_H

#include "hosc_compiler_api.h"

int hosc_cli_command_run(const char* path);
/** If `output_hbc` is non-NULL, write bytecode there. Else if `keep_bytecode`, write `<stem>.hbc` beside source. Then run. */
int hosc_cli_command_run_ex(const char* path, const char* output_hbc, int keep_bytecode);
int hosc_cli_command_build(const char* path);
int hosc_cli_command_check(const char* path);
int hosc_cli_command_fmt(const char* path);
int hosc_cli_command_test(void);
int hosc_cli_command_version(void);

void hosc_cli_print_usage(void);
void hosc_cli_print_diagnostics(const HDiagnosticBag* diagnostics);
char* hosc_cli_replace_extension(const char* path, const char* extension);

#endif
