# Setup Guide

This guide covers the current repository layout and toolchain requirements.

## Supported Environment

The checked-in scripts and framework backend are Windows-first.

| Tool | Used by | Notes |
| --- | --- | --- |
| PowerShell 5.1+ | `tools/build.ps1`, `framework/build.ps1`, quality gate | Run from the repository root unless noted otherwise. |
| MinGW GCC | Native compiler, VM, runtime, CLI, framework | `gcc` and `ar` must be available on `PATH`. |
| CMake 3.20+ | Top-level CMake build | Builds `compiler`, `vm`, `runtime`, `cli`, and `tests`. |
| Node.js 20+ | `lsp/`, `vscode-extension/` | Required only for editor tooling. |
| VS Code 1.85+ | `vscode-extension/` | Required only to run or package the extension. |

No Python virtual environment is required for the core build. Python helper
tools exist under `tools/`, including `hosc_ide.py` and `vscode_mini.py`.

## Native Toolchain Setup

Install MinGW and verify the compiler:

```powershell
gcc --version
ar --version
```

Build the bootstrap native tools:

```powershell
.\tools\build.ps1
```

Expected synchronized binaries:

```text
tools/bin/hosc.exe
tools/bin/hvm.exe
tools/bin/hvm_host.exe
```

## Framework Setup

The framework is a separate GUI/event runtime and is not included in the
top-level CMake build.

```powershell
.\framework\build.ps1
.\framework\bin\hosc_framework.exe run .\framework\examples\hello_world_window.hosc
```

Framework assets used by examples are stored in `framework/examples/asset/`.

## Editor Tooling Setup

Build the language server:

```powershell
Push-Location lsp
npm install
npm run build
Pop-Location
```

Build the VS Code extension:

```powershell
Push-Location vscode-extension
npm install
npm run build
Pop-Location
```

The extension can auto-detect `hosc.exe` from `PATH`, workspace build outputs,
or `tools/bin/hosc.exe`. You can also set `hosc.executablePath` in VS Code.

## First Verification

```powershell
.\tools\bin\hosc.exe version
.\tools\bin\hosc.exe check .\framework\examples\Hello.hosc
.\tools\bin\hosc.exe run .\framework\examples\Hello.hosc
```

The current version command prints `HOSC 0.2.0` from
`cli/hosc/src/command_version.c`.
