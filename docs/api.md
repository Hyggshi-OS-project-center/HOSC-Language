# API Reference

This reference documents public surfaces that exist in the current source tree.

## CLI

Executable:

```text
tools/bin/hosc.exe
```

Commands from `cli/hosc/src/main.c` and `cli/hosc/src/cli_output.c`:

| Command | Status | Behavior |
| --- | --- | --- |
| `hosc run <file.hosc> [-o out.hbc] [--keep]` | Implemented | Compiles source and runs bytecode through the runtime. `-o` writes bytecode to a chosen file. `--keep` writes `<source>.hbc`. |
| `hosc build <file.hosc>` | Implemented | Compiles source and writes `<source>.hbc`. |
| `hosc check <file.hosc>` | Implemented | Runs compilation in check-only mode and prints diagnostics on failure. |
| `hosc fmt <file.hosc>` | Stub | Returns `formatter not implemented in bootstrap build`. |
| `hosc test` | Stub | Returns `use CTest for the bootstrap build`. |
| `hosc version` | Implemented | Prints `HOSC 0.2.0`. |

`hosc --version` is not handled by the current CLI parser.

## Compiler C API

Header: `compiler/include/hosc_compiler_api.h`

```c
typedef struct HoscCompileOptions {
    const char* module_name;
    const char* output_path;
    bool emit_debug_info;
    bool check_only;
} HoscCompileOptions;

typedef struct HoscCompileResult {
    bool success;
    HBytecode* bytecode;
    HDiagnosticBag* diagnostics;
} HoscCompileResult;

HoscCompileResult hosc_compile_file(const char* path, const HoscCompileOptions* options);
HoscCompileResult hosc_compile_memory(
    const char* display_path,
    const char* source,
    size_t length,
    const HoscCompileOptions* options);
bool hosc_write_bytecode_file(const char* path, const HBytecode* bytecode);
void hosc_compile_result_free(HoscCompileResult* result);
```

Current compiler behavior:

- Reads a source file and resolves imports before parsing.
- Supports quoted imports and dotted module imports.
- Requires a `func main` entry point.
- Rejects framework GUI scripts with diagnostic `H003`.
- Emits HBC bytecode from parsed AST functions.

### Diagnostic System

Output format:
```text
<file>:<line>:<col>: <severity> <code>:
<message>

<source_line>
^~~~~
```

Diagnostic Error Codes:

| Code | Severity | Description |
| --- | --- | --- |
| `H000` | Error | File read failure (`cannot open source file`) or import resolution failure (`failed to resolve import`) |
| `H001` | Error | Missing `func main()` entry point |
| `H002` | Error | Syntax parse error (generic/unclassified — falls back to this code when the parser's error message doesn't match a more specific pattern) |
| `H003` | Error | Framework GUI script detected (must run via `hosc_framework`) |
| `H104` | Error | Unknown keyword or keyword typo with recommendation (`did you mean 'var'?`) |
| `H205` | Error | Unknown function or call typo with recommendation (`did you mean 'print'?`) |
| `H900` | Error | Bytecode emission failure |

**Fixed (2026-07-31):** `parser_create()` previously left `Parser.error_message`/`error_line`/`error_column` uninitialized (plain `malloc`, no zeroing). Any parse function that returned `NULL` without first calling `parser_set_error(...)` would leak whatever garbage bytes were on the heap as the diagnostic text, and the location would fall back to `1:1` instead of the real failure point (e.g. `hello.hosc:1:1: error H002: @nRx\`). `parser_create()` now zero-initializes the struct and defaults the location to `1:1` only as an intentional fallback, so an unset error message reads as empty rather than garbage. The specific `prints[...]` missing-`[` path in `parse_prints()` (`compiler/src/parser.c`) was also updated to call `parser_set_error(...)` instead of returning `NULL` silently.

## Runtime C API


Header: `runtime/include/hosc_runtime_api.h`

```c
typedef struct HoscRuntimeOptions {
    bool trace;
    bool enable_gc;
} HoscRuntimeOptions;

int hosc_runtime_run_file(const char* path, const HoscRuntimeOptions* options);
int hosc_runtime_run_bytecode(const HBytecode* bc, const HoscRuntimeOptions* options);
```

Embedding header: `runtime/include/hosc_embed.h`

```c
typedef struct HoscRuntime {
    HVM* vm;
} HoscRuntime;

HoscRuntime* hosc_runtime_create(void);
void hosc_runtime_destroy(HoscRuntime* runtime);
bool hosc_runtime_register_native(HoscRuntime* runtime, const char* name, int arity, HNativeFn fn);
int hosc_runtime_execute(HoscRuntime* runtime, const HBytecode* bytecode);
```

## VM API

Header: `vm/include/hvm_api.h`

```c
HVM* hvm_create(const HVMConfig* config);
void hvm_destroy(HVM* vm);
bool hvm_register_native(HVM* vm, const char* name, int arity, HNativeFn fn);
bool hvm_load_bytecode(HVM* vm, const HBytecode* bc);
int hvm_execute(HVM* vm, const HBytecode* bc);
int hvm_execute_entry(HVM* vm);
const char* hvm_last_error(HVM* vm);
```

The VM includes:

- stack limit `HVM_STACK_MAX = 1024`
- call frame limit `HVM_FRAMES_MAX = 256`
- native registry
- object list and mark-sweep GC fields
- runtime tracing flag

## Bytecode Format

Header: `compiler/include/hosc_bytecode.h`

| Field | Current value |
| --- | --- |
| Magic | `HBC0` |
| Version | `0.1` |
| Constants | int, float, string tags |
| Symbols | global symbol table with mutability flag |
| Functions | name index, arity, locals, max stack, code offset, code size |

Opcodes currently include constants, literals, stack operations, locals,
globals, arithmetic, equality, comparisons, jumps, calls, return, and halt.

## Framework Runtime Surface

The framework is documented in `framework/docs/README_FRAMEWORK.md`. It supports
window creation, text drawing, images, audio playback, message boxes, event
handlers, and loop forms. It should be invoked with:

```powershell
.\framework\bin\hosc_framework.exe run .\framework\examples\hello_world_window.hosc
```

## Editor API Surface

The LSP server under `lsp/` provides:

- incremental document sync
- diagnostics through `hosc check`
- hover
- completions
- document formatting through `hosc fmt`

Because `hosc fmt` is a bootstrap stub, formatting requests can fail until the
formatter is implemented.

The VS Code extension contributes:

- `.hosc` language registration
- TextMate grammar
- `HOSC: Restart Language Server`
- `Run HOSC File`
- `Build HOSC File`
- `Debug Run HOSC File`
- `HOSC: Check Current File`
- `F5` debug binding for HOSC files
