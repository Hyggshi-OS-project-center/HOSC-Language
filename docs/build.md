# Build Guide

HOSC currently has three build surfaces: the PowerShell bootstrap build, the
top-level CMake build, and the standalone framework build.

## Bootstrap Build

Use this path for day-to-day Windows development:

```powershell
.\tools\build.ps1
```

Optional parameters:

```powershell
.\tools\build.ps1 -Configuration Debug
.\tools\build.ps1 -Configuration Release -Clean
.\tools\build.ps1 -RunTests
.\tools\build.ps1 -Compiler gcc -Archiver ar
```

Outputs:

| Path | Purpose |
| --- | --- |
| `build/bootstrap/obj/` | Intermediate object files. |
| `build/bootstrap/lib/libhosc_compiler.a` | Static compiler library. |
| `build/bootstrap/lib/libhvm.a` | Static VM library. |
| `build/bootstrap/lib/libhosc_runtime.a` | Static runtime library. |
| `build/bootstrap/bin/hosc.exe` | CLI executable. |
| `build/bootstrap/bin/hvm_host.exe` | Bytecode host executable. |
| `tools/bin/hosc.exe` | Legacy synchronized CLI path. |
| `tools/bin/hvm.exe` | Legacy synchronized host path. |
| `tools/bin/hvm_host.exe` | Legacy synchronized host path. |

The current bootstrap script does not produce `hosc-compiler.exe`.

## CMake Build

```powershell
cmake -S . -B build/cmake -G Ninja
cmake --build build/cmake
ctest --test-dir build/cmake --output-on-failure
```

Top-level targets:

| Target | Type | Source |
| --- | --- | --- |
| `hosc_compiler` | Static library | `compiler/` |
| `hvm` | Static library | `vm/` |
| `hosc_runtime` | Static library | `runtime/` |
| `hvm_host` | Executable | `runtime/src/entry/main_host.c` |
| `hosc` | Executable | `cli/hosc/` |
| `security_regression` | Test executable | `tests/security_regression.c` |

`HOSC_BUILD_TESTS` defaults to `ON`.

```powershell
cmake -S . -B build/cmake -DHOSC_BUILD_TESTS=OFF
```

## Framework Build

PowerShell:

```powershell
.\framework\build.ps1
```

Make:

```powershell
make -f framework/Makefile.framework framework
```

Direct GCC command used by the docs and framework script:

```powershell
gcc -Wall -Wextra -std=c99 -O2 -Iframework\include -o framework\bin\hosc_framework.exe framework\src\hosc_framework.c framework\src\hosc_runtime.c framework\src\hosc_modules.c -luser32 -lgdi32 -lkernel32 -lgdiplus -lole32 -lcomdlg32 -lmfplay -lmfplat -lmf -lmfuuid
```

## Editor Builds

Language server:

```powershell
Push-Location lsp
npm install
npm run build
Pop-Location
```

VS Code extension:

```powershell
Push-Location vscode-extension
npm install
npm run build
Pop-Location
```

## Quality Gate

`tools/quality_gate.ps1` builds the project and exercises the current CLI
surface:

- `hosc version`
- `hosc check`
- `hosc build`
- `hosc run`
- `hosc run -o`
- a missing-`func main` diagnostic path

```powershell
.\tools\quality_gate.ps1
```

## CI

`.github/workflows/nextgen-ci.yml` runs on Windows, installs MinGW, builds with
`tools/build.ps1`, smoke-tests the CLI, and uploads native artifacts.
