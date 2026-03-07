<<<<<<< HEAD
# HOSC Language

Project structure:

- compiler/: HOSC compiler (lexer/parser/AST/codegen + compiler frontend)
- runtime/: VM/runtime (HVM + bytecode runner/compiler backend)
- framework/: GUI/system framework layer
- examples/: HOSC demo scripts
- docs/: documentation
- tools/: build scripts, tests, and legacy/debug helpers

## Quick Build (Windows + MinGW gcc)

```powershell
cd tools
.\build.ps1
```

Outputs:

- `tools/bin/hosc-compiler.exe`
- `tools/bin/hvm.exe`
- `tools/bin/hosc.exe`

## Quick Run (Unified CLI)

```powershell
# show CLI version
.\tools\bin\hosc.exe --version
# or
.\tools\bin\hosc.exe version

# syntax/type check (compile-only validation)
.\tools\bin\hosc.exe check framework\examples\smoke.hosc

# format source in-place
.\tools\bin\hosc.exe fmt framework\examples\smoke.hosc

# verify formatting only (exit 1 if needs format)
.\tools\bin\hosc.exe fmt framework\examples\smoke.hosc --check

# build bundled executable (.exe)
.\tools\bin\hosc.exe build framework\examples\smoke.hosc

# build + run on VM
.\tools\bin\hosc.exe run framework\examples\smoke.hosc
```

Optional flags:

- `build` default output: `<input>.exe` (bundled runtime + bytecode)
- `build -o <file.hbc>`: emit raw bytecode instead of `.exe`
- `run -o <file.hbc>`: custom bytecode output path for run pipeline
- `--keep`: keep temp `.hbc` when using `run` without `-o`
- `fmt -o <file.hosc>`: write formatted output to another file

## Manual Pipeline

```powershell
.\tools\bin\hosc-compiler.exe framework\examples\smoke.hosc -b smoke.hbc
.\tools\bin\hvm.exe smoke.hbc
```

## Stability Gate

Run full P0 smoke gate (build + check/build/run + lexer fail-fast + fmt validation):

```powershell
.\tools\quality_gate.ps1
```

Framework maturity checklist:

- `docs/FRAMEWORK_MATURITY.md`

## VSCode Mini (Python GUI)

Requirements:

- Python 3 + PyQt5 (`pip install PyQt5`)
- `tools/bin/hosc-compiler.exe` and `tools/bin/hvm.exe` already built

Run:

```powershell
.\tools\build.ps1
python .\tools\vscode_mini.py
# or
.\tools\vscode_mini.bat
```

Main features:

- Qt5 dark UI (explorer + editor tabs + output panel)
- File menu (New/Open/Save/Save As)
- File explorer double-click to open .hosc
- Autocomplete (Ctrl+Space + inline suggestions while typing)
- Error underline (red wave) after failed build / live lint
- Build/Run integration to HOSC compiler + HVM
- Build/Run logs in output panel
- Open a file directly: `python .\tools\vscode_mini.py framework\examples\smoke.hosc`

Current real GUI features:

- Win32 dropdown menu (`File > New/Open/Save/Exit`)
- Vertical scrollbar with mouse wheel + scroll bar drag
- Real open/save file dialogs
- Runtime file APIs from HOSC (`open_file_dialog`, `save_file_dialog`, `file_read`, `file_read_line`, `file_write`)
- Runtime shell command API from HOSC (`exec`) for Build/Run inside editor
- Multiline editor widget APIs (`textarea`, `textarea_set`, `code_editor`)

Menu event IDs exposed to script:

- `1001` = New
- `1002` = Open
- `1003` = Save
- `1004` = Exit


=======
# HOSC-Language
>>>>>>> 995adf4f7dd3fdb25496c878c7f120fb6d0ed860
