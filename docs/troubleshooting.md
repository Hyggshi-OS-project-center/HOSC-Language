# Troubleshooting

This file lists issues observed from the current repository layout and scripts.

## `gcc` or `ar` Is Not Found

Install MinGW and ensure the binary directory is on `PATH`.

```powershell
gcc --version
ar --version
```

Then rerun:

```powershell
.\tools\build.ps1
```

## `hosc-compiler.exe` Is Missing

`tools/build.ps1` currently builds static libraries, `hosc.exe`, and
`hvm_host.exe`, then syncs:

- `tools/bin/hosc.exe`
- `tools/bin/hvm.exe`
- `tools/bin/hvm_host.exe`

It does not currently build `tools/bin/hosc-compiler.exe`. Any script or doc
that requires that binary should be updated or the binary target should be
reintroduced intentionally.

## `hosc --version` Fails

Use:

```powershell
.\tools\bin\hosc.exe version
```

The current CLI parser supports `version`, not `--version`.

## `hosc build -o out.hbc` Fails

`build` currently accepts only:

```powershell
.\tools\bin\hosc.exe build path\to\file.hosc
```

Use `run` when a custom bytecode output path is needed:

```powershell
.\tools\bin\hosc.exe run path\to\file.hosc -o build\out.hbc
```

## `hosc fmt` Fails

This is expected in the bootstrap build:

```text
hosc fmt: formatter not implemented in bootstrap build
```

The LSP and VS Code extension expose formatting, but formatting will not work
until the CLI formatter is implemented.

## `hosc test` Fails

This is expected in the bootstrap build:

```text
hosc test: use CTest for the bootstrap build
```

Use CTest instead:

```powershell
cmake -S . -B build/cmake -G Ninja
cmake --build build/cmake
ctest --test-dir build/cmake --output-on-failure
```

## Framework Script Fails in `hosc.exe`

GUI scripts are not run through the main compiler pipeline. Use the framework
runner:

```powershell
.\framework\build.ps1
.\framework\bin\hosc_framework.exe run .\framework\examples\hello_world_window.hosc
```

## `%` (Modulo) Always Fails With `H900`

Any expression using `%` fails to compile:

```text
hello.hosc:1:1: error H900:
failed to emit bytecode
```

`%` is a real token (`TOKEN_PERCENT`) and parses into a valid
`AST_BINARY_OP` node — it passes lexing, parsing, and type-checking. The
bug is in the bytecode emitter: the binary-op switch in
`compile_expression()` (`compiler/src/frontend/pipeline.c`) only has cases
for `TOKEN_PLUS`, `TOKEN_MINUS`, `TOKEN_STAR`, `TOKEN_SLASH`, and the
comparison operators. There is no `case TOKEN_PERCENT`, so it falls to
`default: return 0;`, which the caller reports as a generic "failed to emit
bytecode".

**Workaround until fixed:** avoid `%` in HOSC source; compute a remainder
manually (e.g. `n - (n / d) * d` using integer division) if you need one.

## Stale Example Paths

The current `examples/` directory is empty in this workspace snapshot, while
older docs and scripts may still refer to:

- `examples/hello_world/hello.hosc`
- `examples/level_a/hello.hosc`
- `framework/examples/window.hosc`
- `framework/examples/smoke.hosc`

Verified current examples include:

- `framework/examples/Hello.hosc`
- `framework/examples/hello_world_window.hosc`
- `framework/examples/Music.hosc`
- `framework/examples/Easter_Egg.hosc`

## `error H002:` Shows Garbage Text Instead of a Message

If a parse error prints unreadable/garbage bytes as the message, with the
location stuck at `1:1` regardless of where the real problem is, e.g.:

```text
hello.hosc:1:1: error H002:
@nRx\
// Hello World in HOSC
^
```

This was a real bug: `parser_create()` in `compiler/src/parser.c` allocated
the `Parser` struct with `malloc` and never zeroed it, so `error_message`,
`error_line`, and `error_column` started as uninitialized heap memory. Any
parse function that returned `NULL` without calling `parser_set_error(...)`
first (e.g. the `prints[...]` missing-`[` path) leaked that garbage straight
to the diagnostic output.

**Fixed 2026-07-31:** `parser_create()` now zero-initializes the struct and
defaults the location to `1:1` only as an intentional fallback; the
`prints[...]` path now calls `parser_set_error(...)` instead of returning
silently. If you still see this, rebuild against the current `parser.c` —
older binaries built before the fix will still show the garbage.

## LSP Cannot Find `hosc.exe`

Build the native CLI first:

```powershell
.\tools\build.ps1
```

Then either ensure `tools/bin/hosc.exe` is discoverable from the workspace or set
`hosc.executablePath` in VS Code.

## Cleanup Suggestions

The scan found these cleanup candidates:

| Item | Reason |
| --- | --- |
| Generated build directories | `build/`, `framework/build/`, `lsp/out/`, and `vscode-extension/out/` should be treated as generated artifacts. |
| Checked-in binaries | `tools/bin/*.exe` and `framework/bin/hosc_framework.exe` may be useful locally but should be intentionally managed before public release. |
| Empty `examples/` directory | Either restore canonical examples or update all tests/docs to use `framework/examples/Hello.hosc`. |
| Stale framework examples | Some older project history references `window.hosc` and `smoke.hosc`, which are absent in this snapshot. |
| Older generated outputs | Regenerate local `.hbc` files and binaries after source changes instead of treating them as source of truth. |
| Older language references | `HOSC_QUICK_REFERENCE.md` and `HOSC_SYNTAX_REFERENCE.md` may describe aspirational syntax. Keep them clearly marked until compiler support catches up. |
