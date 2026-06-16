# Roadmap

This roadmap reflects gaps visible in the current source tree and docs. It is
not a promise of release dates.

## Near Term

- Implement or intentionally remove `hosc fmt` from editor formatting paths.
- Decide whether `hosc test` should run project tests or remain a CTest pointer.
- Align `tools/quality_gate.ps1` with `tools/build.ps1` outputs.
- Restore canonical examples under `examples/` or update all tests to use
  current `framework/examples/` files.
- Add focused tests for lexer, parser, import resolution, bytecode emission, and
  VM execution.
- Mark older syntax reference pages as implemented, planned, or legacy.

## Mid Term

- Expand compiler support beyond the current bootstrap language subset.
- Stabilize diagnostic output so CLI, LSP, and tests share one format.
- Add package/module tests for quoted imports and dotted imports.
- Package the VS Code extension with a repeatable release process.
- Add generated API docs for public C headers.
- Build framework smoke tests that do not require interactive GUI inspection.

## Long Term

- Define a formal HOSC language specification.
- Stabilize the HBC bytecode format and versioning policy.
- Support non-Windows build and runtime targets where feasible.
- Add release packaging for native binaries and editor tooling.
- Document a standard library once module boundaries are stable.
