# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Development Commands

### Building
- **Full bootstrap build (Windows + MinGW)**:
  ```powershell
  cd tools
  .\build.ps1
  ```
  Produces `tools/bin/hosc-compiler.exe`, `tools/bin/hvm.exe`, and `tools/bin/hosc.exe`.

- **Clean build**:
  ```powershell
  cd tools
  .\build.ps1 -Clean
  ```

- **Build with tests**:
  ```powershell
  cd tools
  .\build.ps1 -RunTests
  ```

### Unified CLI (`hosc.exe`)
After building, use the unified CLI for common operations:
- **Version**: `.\tools\bin\hosc.exe --version` or `version`
- **Syntax/type check**: `.\tools\bin\hosc.exe check <file.hosc>`
- **Format source**: `.\tools\bin\hosc.exe fmt <file.hosc>`
- **Verify formatting**: `.\tools\bin\hosc.exe fmt <file.hosc> --check`
- **Build bundled executable**: `.\tools\bin\hosc.exe build <file.hosc>`
- **Build + run on VM**: `.\tools\bin\hosc.exe run <file.hosc>`
- **Custom output paths**: Use `-o <file>` with `build` or `run` to specify output
- **Keep temporary bytecode**: Use `--keep` with `run` to preserve `.hbc`

### Manual Pipeline
```powershell
.\tools\bin\hosc-compiler.exe <file.hosc> -b <output.hbc>
.\tools\bin\hvm.exe <output.hbc>
```

### Quality Gate
Run full P0 smoke gate (build + check/build/run + lexer fail-fast + fmt validation):
```powershell
.\tools\quality_gate.ps1
```

### VSCode Mini GUI
Requires Python 3 + PyQt5 and pre-built compiler/VM:
```powershell
cd tools
.\build.ps1
python .\vscode_mini.py
# Or open a file directly:
python .\vscode_mini.py framework\examples\smoke.hosc
```

## Project Structure

### Core Components
- **`compiler/`**: HOSC compiler frontend and middle-end
  - Lexer, parser, AST, semantic analysis, IR, bytecode codegen
  - Key directories: `lexer/`, `parser/`, `ast/`, `sema/`, `ir/`, `codegen/`, `module/`, `frontend/`
- **`runtime/`**: HOSC virtual machine and runtime support
  - Bytecode interpreter, garbage collector, native API, platform abstraction
  - Key directories: `vm/` (object model, memory, core), `bundle/` (executable bundling), `embed/` (embedding API), `entry/` (bundle loader, host main), `platform/` (Win32 platform layer)
- **`framework/`**: GUI/system framework layer
  - Provides higher-level APIs for windowing, events, drawing
- **`examples/`**: HOSC demo scripts demonstrating language and framework features
- **`tools/`**: Build scripts, test helpers, and utilities
  - `build.ps1`: Main bootstrap build script
  - `quality_gate.ps1`: Stability gate runner
  - `vscode_mini.py`: Python/Qt5-based IDE for HOSC development
  - `hosc_cli.c`: Unified CLI implementation
- **`docs/`**: Documentation
  - `architecture/`: System design documents
  - `language/`: Language specification references

### Key Artifacts
- **Compiler**: `hosc-compiler.exe` (translates `.hosc` → `.hbc` bytecode)
- **VM**: `hvm.exe` (executes `.hbc` bytecode)
- **Unified CLI**: `hosc.exe` (combines check/fmt/build/run commands)
- **Host Executable**: `hvm_host.exe` (minimal host for bundled executables)

### Build System
- Uses a custom PowerShell bootstrap build (`tools/build.ps1`)
- Compiles C sources with MinGW gcc (configurable via `$Compiler` parameter)
- Produces static libraries (`libhosc_compiler.a`, `libhvm.a`, `libhosc_runtime.a`) then links executables
- Supports Debug (`-O0 -g3`) and Release (`-O2`) configurations

### Conventions
- Source files use `.c` extension, headers `.h`
- Error handling via diagnostic system (`compiler/src/diag/`)
- Memory management: VM uses mark-sweep garbage collector
- Platform abstraction: runtime isolates platform-specific code in `runtime/src/platform/`