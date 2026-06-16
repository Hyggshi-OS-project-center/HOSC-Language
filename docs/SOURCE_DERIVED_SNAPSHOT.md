# HOSC Source-Derived Snapshot

This document describes the repository as it exists in the current source tree. It is intentionally grounded in:

- `CMakeLists.txt` and per-module `CMakeLists.txt`
- `tools/build.ps1`
- the current CLI sources in `cli/hosc/src`
- the compiler/runtime/VM public headers and entry points
- direct command checks against the bootstrap binaries already present in the repo

## What This Repo Is Right Now

HOSC is currently a bootstrap-stage language toolchain with four core native modules:

- `compiler/`: emits HOSC bytecode via `hosc_compiler`
- `vm/`: defines and executes the HVM bytecode machine
- `runtime/`: loads bytecode, creates the VM, and hosts executable entry points
- `cli/`: exposes the bootstrap `hosc` command

There is also a separate `framework/` subtree for GUI/event demos. It is not the same pipeline as `hosc -> bytecode -> hvm`.

## Build Graph

Top-level CMake builds these targets:

- `hosc_compiler` static library from `compiler/`
- `hvm` static library from `vm/`
- `hosc_runtime` static library from `runtime/`
- `hosc` executable from `cli/`
- `hvm_host` executable from `runtime/src/entry/main_host.c`

`tests/` is only added when `HOSC_BUILD_TESTS=ON`, but the current `tests/` directory contains only `tests/CMakeLists.txt`.

## Bootstrap Build Script

`tools/build.ps1` is the most concrete build path in this repo today.

It:

1. Compiles C sources with GCC.
2. Produces static libraries under `build/bootstrap/lib/`.
3. Produces executables under `build/bootstrap/bin/`.
4. Copies those executables into `tools/bin/` for legacy tooling compatibility.

Expected synced binaries:

- `tools/bin/hosc.exe`
- `tools/bin/hvm.exe`
- `tools/bin/hvm_host.exe`

Note that the current bootstrap script does not build `hosc-compiler.exe`.

## Command Surface That Actually Exists

The CLI entry point is `cli/hosc/src/main.c`.

Supported commands:

- `hosc run <file.hosc> [-o out.hbc] [--keep]`
- `hosc build <file.hosc>`
- `hosc check <file.hosc>`
- `hosc fmt <file.hosc>`
- `hosc test`
- `hosc version`

Observed behavior from the checked-in bootstrap binary:

- `hosc version` works and prints `hosc 0.1.0-bootstrap`
- `hosc check <file>` works for supported bootstrap input
- `hosc build <file>` writes `<file>.hbc`
- `hosc run <file>` compiles in memory and executes on HVM
- `hosc fmt <file>` is a stub and currently fails with `formatter not implemented in bootstrap build`
- `hosc test` is a stub and currently tells the user to use CTest
- `hosc --version` is not supported by the current `main.c`
- `hosc build -o ...` is not supported by the current `main.c`

## Compiler Reality

The public compiler API is exposed through `compiler/include/hosc_compiler_api.h`.

The current bootstrap implementation in `compiler/src/frontend/pipeline.c` is intentionally narrow:

- it expects source containing `func main`
- it extracts the first `print("...")` string literal
- it emits a tiny bytecode program that loads the builtin `print`, pushes the literal, calls it, pops, and returns

Current bootstrap diagnostics include:

- `H000`: source file could not be read
- `H001`: missing `func main()`
- `H002`: unsupported bootstrap input outside the small `print("...")` subset
- `H003`: framework script detected, should be run through `framework/bin/hosc_framework.exe`
- `H900`: bootstrap bytecode allocation failure

`compiler/src/sema/type_checker.c` is still a placeholder, which matches the bootstrap state.

## Runtime and VM Flow

The runtime API lives in `runtime/include/hosc_runtime_api.h`.

Execution path:

1. `hosc run` calls `hosc_compile_file(...)`
2. when compilation succeeds, `hosc_runtime_run_bytecode(...)` creates an `HVM`
3. `hvm_execute(...)` runs the bytecode
4. the runtime reports `runtime error: ...` if the VM returns non-zero

`hvm_host` is a minimal bytecode host that runs `hosc_runtime_run_file(<file.hbc>, NULL)`.

## Verified Examples

Plain bootstrap example:

- `framework/examples/Hello.hosc`

Source:

```hosc
package main

func main() {
    print("Hello World!")
}
```

Verified commands:

```powershell
.\tools\bin\hosc.exe version
.\tools\bin\hosc.exe check .\framework\examples\Hello.hosc
.\tools\bin\hosc.exe build .\framework\examples\Hello.hosc
.\tools\bin\hosc.exe run .\framework\examples\Hello.hosc
```

Framework example:

- `framework/examples/hello_world_window.hosc`

Verified command:

```powershell
.\framework\bin\hosc_framework.exe run .\framework\examples\hello_world_window.hosc
```

This path initializes the separate framework runtime and opens a Win32 window when available.

## Framework Boundary

`framework/` is not compiled by the top-level CMake file.

It has its own:

- source files under `framework/src/`
- include files under `framework/include/`
- docs under `framework/docs/`
- executable `framework/bin/hosc_framework.exe`

Use it for scripts containing GUI/event markers such as:

- `window(...)`
- `text(...)`
- `loop(...)`
- `pump_events(...)`
- `on_click(...)`
- `on_key(...)`
- `on_mouse_move(...)`
- `win32_message_box(...)`

The bootstrap compiler explicitly detects these markers and redirects the user to the framework runner.

## Editor Tooling Status

The repository also contains:

- `lsp/`: a TypeScript language server
- `vscode-extension/`: a VS Code client for that language server
- `tools/hosc_ide.py`: a PyQt5 mini IDE

Current caveats derived from source:

- the LSP formatter shells out to `hosc fmt`, but `hosc fmt` is currently a stub
- the LSP diagnostics parser expects `file:line:col:` style output, while the bootstrap CLI currently prints diagnostics as `error CODE at line:col: ...`
- `tools/hosc_ide.py` still references older example locations such as `examples/level_a`

That means editor support exists in structure, but the bootstrap CLI surface does not yet fully satisfy all editor expectations.

## Documentation Trust Order

When docs disagree, trust them in this order:

1. source files and headers
2. `tools/build.ps1`
3. runnable binaries in `tools/bin/` and `framework/bin/`
4. this snapshot
5. older reference docs

## Short Status Summary

The codebase already has a clean module split and a runnable bootstrap path. What it does not yet have is feature parity between the documented language surface, the editor tooling, and the current bootstrap compiler implementation.
