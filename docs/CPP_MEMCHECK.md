# C++ Memory Check Compiler Gate

Script: `tools/cpp_memcheck.ps1`

## Purpose
Compile C++ source with memory-safety checks **before run**.

- Preferred mode: Address/UB sanitizer (`-fsanitize=address,undefined`)
- Fallback mode (when sanitizer runtime is unavailable): static analyzer (`-fanalyzer`)

If memory issues are detected, the script exits non-zero and blocks run.

## Usage

```powershell
# Compile + check + run
.\tools\cpp_memcheck.ps1 .\path\to\main.cpp

# Compile only (no run)
.\tools\cpp_memcheck.ps1 .\path\to\main.cpp -CompileOnly

# Custom output binary
.\tools\cpp_memcheck.ps1 .\path\to\main.cpp .\build\app.exe

# Extra compiler flags
.\tools\cpp_memcheck.ps1 .\path\to\main.cpp -ExtraFlags "-I.\\runtime\\include"
```

## Output artifacts
Default directory:

- `build/cpp_memcheck/*.exe` or `*.o`
- `build/cpp_memcheck/compile_stderr.log`
- `build/cpp_memcheck/run_stdout.log`
- `build/cpp_memcheck/run_stderr.log`

## Exit codes
- `0`: check passed
- `1`: compile failed, analyzer detected memory-risk warning, or run detected memory error/non-zero exit
