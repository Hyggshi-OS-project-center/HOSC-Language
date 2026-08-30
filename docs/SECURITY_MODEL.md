# HOSC VM Security Model

> Last updated: 2026-08-01 — Security Audit Cycle 3

---

## Overview

This document outlines the security controls active in the **HOSC Virtual Machine (HVM)** and compiler pipeline, as well as planned architectural security enhancements.

---

## Active Security Protections (Implemented)

### 1. Native Audio Command Injection Prevention (`audio.play`)

- **Windows MCI Backend** (`vm/src/native/native_registry.c`): Uses `mciSendCommandA()` with structured `MCI_OPEN_PARMSA` and `MCI_PLAY_PARMS` parameters. Audio paths are passed directly via struct fields rather than being interpolated into shell command strings (`mciSendStringA`). Paths containing double quotes (`"`) are explicitly rejected.
- **POSIX Backend** (`vm/src/native/native_registry.c`): Uses direct process execution via `fork()` and `execvp()` with NULL-terminated argument vectors (`argv`), completely bypassing `/bin/sh` or system shell interpolation.

### 2. Parser Recursion Depth Limit

- **Depth Guard** (`compiler/src/parser.c` & `compiler/include/parser.h`): Enforces `MAX_PARSE_DEPTH = 256`. Deeply nested source syntax (such as `(((...)))` nested 300 times) is caught before call-stack exhaustion and produces a clean diagnostic error:
  ```
  error: Maximum parse depth exceeded
  ```

### 3. Bytecode Loader Bounds & Overflow Hardening

- **Loader Safety** (`vm/src/bytecode/loader.c`): The active bytecode loader validates section counts, string lengths, function table indices, and code section ranges prior to execution:
  - `HBC_MAX_SECTION_COUNT` (65,536 items)
  - `HBC_MAX_STRING_LENGTH` (16 MiB)
  - `HBC_MAX_CODE_SIZE` (64 MiB)
- All section size allocations verify `count * sizeof(...)` against integer overflow (`hvm_checked_section_size`).
- File offset bounds are verified against remaining file size (`hvm_file_has_remaining`) before every read.

### 4. Dead Code & Confusion Hazard Policy

- Unbuilt runtime files (`runtime/src/hvm_*.c`, `bytecode_io.c`, `executor.c`, etc.) from the legacy HBC1 VM were deleted to prevent security regression or reintroduction of unmaintained execution paths.
- Unbuilt standalone compiler implementations (`hosc_compiler.c`) were moved out of `compiler/src/` to `legacy/standalone-compiler/`.

---

## Planned Architecture & Roadmap

The following security features are specified for future releases and are currently tracked on the development roadmap:

### 1. Capability Permission Layer (Planned)

A fine-grained permission model for host application embedding, allowing capability-based sandboxing:

```c
/* PLANNED API - Tracked on Roadmap */
HVMCapabilities caps = hvm_capabilities_default(); // default: all deny
caps.filesystem_read = true;
HVM *vm = hvm_create_with_capabilities(&caps);
```

| Capability        | Planned Default | Current Status |
|-------------------|-----------------|----------------|
| Filesystem read   | ❌ DENY         | ⏳ Roadmap      |
| Filesystem write  | ❌ DENY         | ⏳ Roadmap      |
| Shell execution   | ❌ DENY         | ⏳ Roadmap      |
| Process spawn     | ❌ DENY         | ⏳ Roadmap      |
| Network I/O       | ❌ DENY         | ⏳ Roadmap      |

### 2. Further Enhancements

- [ ] System syscall filtering (`seccomp-BPF` on Linux, App Sandbox on macOS)
- [ ] Bytecode cryptographic signature verification (`.hbc` signing)
- [ ] Compiler path whitelist (restricting external C compiler invocations to pre-approved binary paths)
