# HOSC Insider README

This document is for internal maintainers (compiler/runtime/framework contributors).

## 1) System Overview

HOSC is now a full platform stack:

- Language front-end: lexer + parser + AST
- Compiler: HOSC -> HBC (bytecode)
- Runtime: HVM (stack VM)
- GC: mark-sweep for VM string values
- GUI backend: basic widgets/event/layout runtime
- Packaging: output `.exe` by bundling runtime + bytecode

## 2) Main Pipeline

### Build development binaries

```powershell
.\tools\build.ps1
```

Outputs:

- `tools/bin/hosc-compiler.exe`
- `tools/bin/hvm.exe`
- `tools/bin/hosc.exe`

### CLI workflow

```powershell
# check
.\tools\bin\hosc.exe check <file.hosc>

# quick run
.\tools\bin\hosc.exe run <file.hosc>

# build bundled exe (default)
.\tools\bin\hosc.exe build <file.hosc>

# build raw bytecode
.\tools\bin\hosc.exe build <file.hosc> -o <file.hbc>
```

## 3) Bundle Executable Format

`hosc build` bundles data in this order:

- `[runtime_exe_bytes]`
- `[hbc_payload_bytes]`
- `"HOSCEXE1"` (8-byte magic)
- `uint64 payload_size`

Related files:

- `tools/hosc_cli.c` (bundle writer)
- `runtime/src/hvm_runner.c` (embedded payload loader)

## 4) Runtime + GC Notes

Current GC behavior:

- applies to `HVM_TYPE_STRING` values in VM stack/global memory
- triggered by allocation threshold (`HVM_GC_INITIAL_THRESHOLD`)
- root scan: stack + globals
- sweep unmarked string objects

Related files:

- `runtime/include/hvm.h`
- `runtime/src/hvm.c`

## 5) GUI + Console Behavior

When running bundled `.exe`:

- if bytecode contains GUI opcodes, runtime auto-hides console window
- to force console for debugging:

```powershell
set HVM_FORCE_CONSOLE=1
```

Related file:

- `runtime/src/hvm_runner.c`

## 6) Debug Flags

- `HOSC_DEBUG=1`: print CLI spawned commands
- `HVM_GUI_DEBUG=1`: enable GUI logs
- `HVM_GUI_HEADLESS=1`: run GUI pipeline without opening a real window (test mode)
- `HVM_FORCE_CONSOLE=1`: do not hide CMD in bundled mode

## 7) Pre-merge Smoke Checklist

```powershell
.\tools\quality_gate.ps1
```

Must pass:

- compiler/runtime/CLI build
- check/build/run basic path
- lexer fail-fast path
- fmt check

Recommended extra checks:

- `hosc build app.hosc` -> `app.exe` runs correctly
- GUI sample does not show extra CMD window (unless forced)

## 8) Current Scope

Stable now:

- P0 stability + deterministic failure paths
- VM bytecode execution + bundled exe output
- automatic GC for VM string values

Not fully complete yet:

- full typed runtime model beyond current scope
- advanced GUI widget set + full layout engine
- production-grade debugger/LSP
