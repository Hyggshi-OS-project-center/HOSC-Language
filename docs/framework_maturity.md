# Framework Maturity (HOSC)

Last updated: 2026-03-07

## Current Verdict

Framework is now at **"stable core"** level for compiler/runtime pipeline, but **not yet "full engine"**.

- Stable for: CLI build/run/check/fmt, core control-flow compile/execute, deterministic lexer failure path.
- Not complete for: rich GUI widgets/layout system, full event model, debugger-grade tooling.

## Maturity Levels

## Level A: Core Stability (must-have)

- [x] Build toolchain from clean workspace (`tools/build.ps1`)
- [x] Unified CLI commands (`run`, `build`, `check`, `fmt`, `version`, `--version`)
- [x] Deterministic lexer failure on bad string input
- [x] Codegen coverage for core statements (`if`, `while`, `for`, `return`, call/expr stmt)
- [x] Runtime execution for assignment + control flow (`if`, `while`) without silent fallback
- [x] Typed value flow in executor call/return path (int/float/string)
- [x] Repeatable smoke gate (`tools/quality_gate.ps1`)

Status: **PASS**

## Level B: Developer Workflow (should-have)

- [x] VSCode extension with Run/Build/Debug commands
- [x] Basic autocomplete + diagnostics channel in extension
- [x] Python mini IDE (Qt5) for lightweight editing
- [ ] Precise diagnostics with full span quality in all tools
- [ ] One-command automated test suite integrated into CI

Status: **PARTIAL**

## Level C: Full GUI Engine (nice-to-have / next)

- [x] Real window + loop + text + button + basic text input API
- [x] File dialog/read/write APIs
- [ ] Full widget set (dropdown/list/checkbox/panel/image/sprite)
- [ ] Robust event API (`mouse_down/up/hover`, key events, focus)
- [ ] Advanced layout (`row/column/grid`) + stable clipping/scroll behavior
- [ ] Renderer/perf profiling + frame-time budget checks

Status: **IN PROGRESS**

## Quality Gate

Run this before release or before merging major runtime/compiler changes:

```powershell
.\tools\quality_gate.ps1
```

Gate covers:

- build of compiler/runtime/CLI
- CLI `check/build/run` happy path
- lexer fail-fast case (unterminated string)
- formatter consistency (`fmt --check` + rewrite + re-check)

## Next High-Impact Work

1. Complete GUI event system (hover/down/up/key/focus) with stable per-frame state.
2. Add multiline editor-grade widget API (`textarea` state model + cursor/selection).
3. Wire full CI script (`build + unit + quality_gate`) for regression prevention.
