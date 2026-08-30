# Changelog

All notable source-derived project changes should be documented here.

## [Unreleased]

### Documentation

- Rebuilt the root README around the current compiler, VM, runtime, CLI,
  framework, LSP, and VS Code extension layout.
- Added source-derived setup, build, API, architecture, and troubleshooting
  documentation under `docs/`.
- Documented current CLI limits: `fmt` and `test` are stubs, `version` is the
  supported version command, and `build` does not accept `-o`.
- Documented the framework as a separate runtime path from the main HVM
  bytecode pipeline.

### Maintenance

- Identified stale example paths, generated artifacts, checked-in binaries, and
  quality-gate mismatch items for cleanup.

## [0.2.0] - 2026-05-16

### Current Snapshot

- CLI version command reports `HOSC 0.2.0`.
- Native modules are split into `compiler`, `vm`, `runtime`, and `cli` CMake
  targets.
- Bootstrap build produces `hosc.exe`, `hvm.exe`, and `hvm_host.exe`.
- TypeScript LSP server and VS Code extension are present for `.hosc` files.

## [0.1.0]

### Initial Bootstrap

- Added early compiler, bytecode VM, runtime, framework experiments, and helper
  tools.
