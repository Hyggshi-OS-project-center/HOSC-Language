# HOSC Language

[![HOSC Language CI](https://github.com/Hyggshi-OS-project-center/HOSC-Language/actions/workflows/nextgen-ci.yml/badge.svg)](https://github.com/Hyggshi-OS-project-center/HOSC-Language/actions/workflows/nextgen-ci.yml)
[![License: Apache-2.0](https://img.shields.io/badge/license-Apache--2.0-blue.svg)](LICENSE)
[![Language: C11](https://img.shields.io/badge/language-C11-555.svg)](CMakeLists.txt)
[![Platform: Windows%20%7C%20Linux](https://img.shields.io/badge/platform-Windows%20%7C%20Linux-lightgrey.svg)](docs/setup.md)

HOSC is a bootstrap-stage programming language toolchain written in C. The
repository currently contains a compiler frontend, HVM bytecode virtual machine,
runtime host, command-line tool, Win32-oriented GUI framework experiments, an
LSP server, and a VS Code extension.

The implementation is in active transition. This README documents the behavior
that is visible in the current source tree, CMake files, PowerShell build
scripts, and checked-in examples.

## Features

| Area | Current status |
| --- | --- |
| Compiler | Parses HOSC source through the C frontend and emits `.hbc` bytecode. |
| Imports | Resolves quoted imports and dotted module imports relative to the source file. |
| HVM | Runs bytecode with call frames, stack values, constants, globals, native calls, and mark-sweep GC scaffolding. |
| Runtime | Provides `hosc_runtime_run_file`, `hosc_runtime_run_bytecode`, and embedding helpers. |
| CLI | Supports `run`, `build`, `check`, `fmt`, `test`, and `version` commands. |
| Framework | Separate GUI/event runtime for window, text, image, audio, message box, and event-loop demos. |
| Editor tooling | TypeScript LSP server plus VS Code language extension for `.hosc` files. |
| CI | Windows GitHub Actions workflow builds with MinGW and smoke-tests the CLI. |

Known limits:

- `hosc fmt` currently returns `formatter not implemented in bootstrap build`.
- `hosc test` currently delegates users to CTest.
- The framework runtime is separate from the main `hosc -> compiler -> HVM` path.
- Older language reference files under `docs/` may describe features that are
  not fully implemented in the bootstrap compiler yet.

## Screenshots

No canonical screenshots are checked in yet. Framework GUI demos live under
`framework/examples/` and can be used to capture release screenshots once the
framework binary has been built.

## Installation

### Prerequisites

- CMake 3.20+ for the CMake build path
- A C11 compiler supported by CMake (MSVC, MinGW GCC, GCC, or Clang)
- Ninja is optional; CMake selects a native generator when it is absent.
- Node.js 20+ for `lsp/` and `vscode-extension/`

### Bootstrap build

```powershell
.\tools\build.ps1
```

The script compiles the compiler, VM, runtime, and CLI with GCC. Outputs are
created under `build/bootstrap/` and synchronized into `tools/bin/`:

- `tools/bin/hosc.exe`
- `tools/bin/hvm.exe`
- `tools/bin/hvm_host.exe`

### CMake build

```sh
cmake -S . -B build/cmake
cmake --build build/cmake
ctest --test-dir build/cmake --output-on-failure
```

The binaries are emitted to `build/cmake/bin/` with the platform-native
extension (`.exe` on Windows, no extension on Linux/macOS).

## Usage

Run the current CLI:

```sh
./build/cmake/bin/hosc version
./build/cmake/bin/hosc check path/to/program.hosc
./build/cmake/bin/hosc build path/to/program.hosc -o build/program.hbc
./build/cmake/bin/hosc run path/to/program.hosc
./build/cmake/bin/hosc run path/to/program.hosc --keep
```

Minimal HOSC source:

```hosc
package main

func main() {
    print("Hello World!")
}
```

Run a framework GUI example:

```sh
make -C framework -f Makefile.framework framework
./framework/bin/hosc_framework run framework/examples/hello_world_window.hosc
```

## Development Setup

1. Clone the repository.
2. Install CMake and a supported C compiler; install PowerShell only if you
   want to use the optional PowerShell build scripts.
3. Build native tooling with the CMake commands above.
4. Optionally build the framework with `make -C framework -f Makefile.framework framework`.
5. Build editor tooling:

```powershell
Push-Location lsp
npm install
npm run build
Pop-Location

Push-Location vscode-extension
npm install
npm run build
Pop-Location
```

More setup notes are available in [docs/setup.md](docs/setup.md).

## Folder Structure

```text
.
|-- cli/                 HOSC command-line executable sources
|-- compiler/            Compiler API, diagnostics, lexer, parser, AST, IR, codegen
|-- docs/                Project, language, architecture, and maintenance docs
|-- framework/           Standalone Win32-oriented GUI/event framework runtime
|-- import_test/         Local import and media test assets
|-- lsp/                 TypeScript Language Server Protocol implementation
|-- runtime/             Runtime host, bytecode runner, embedding API, platform code
|-- tests/               CTest targets and native regression tests
|-- tools/               Bootstrap build scripts, quality gate, helper tools
|-- vm/                  HVM bytecode loader, interpreter, object model, GC, natives
`-- vscode-extension/    VS Code extension and TextMate grammar
```

## Build Instructions

See [docs/build.md](docs/build.md) for native, framework, CI, and editor build
commands.

## Deployment

For an internal Windows release, package the generated binaries from:

- `tools/bin/hosc.exe`
- `tools/bin/hvm.exe`
- `tools/bin/hvm_host.exe`
- `framework/bin/hosc_framework.exe` if GUI demos are part of the release
- `vscode-extension/` after `npm run build` if editor support is included

Do not ship generated build directories, local `.hbc` files, or media test
assets unless they are intentionally part of the release.

## FAQ

**Is HOSC production-ready?**  
No. The repository is a bootstrap toolchain and language/runtime experiment.

**Why are there older docs that describe more syntax than the compiler accepts?**  
The language design docs are ahead of the bootstrap compiler. Trust source,
`docs/SOURCE_DERIVED_SNAPSHOT.md`, and the docs added in this pass first.

**Can the framework examples be run through `hosc`?**  
No. GUI framework scripts should be run with the platform-native
`framework/bin/hosc_framework` executable. The compiler detects framework
markers and reports that boundary.

**Does `hosc build` support `-o`?**  
Yes. Use `hosc build source.hosc -o output.hbc`. Native bundled executable
generation is not implemented; `.hbc` is the portable build artifact.

## Credits

- Project lead: Hyggshi OS Developer
- Toolchain, framework, LSP, and extension contributors: see repository history

## License

This project is licensed under the Apache License 2.0. See [LICENSE](LICENSE).
