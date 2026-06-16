# Contributing

Thanks for contributing to HOSC. This repository is a bootstrap-stage language
toolchain, so small, well-scoped changes with clear tests are preferred.

## Development Setup

1. Install PowerShell, MinGW GCC, CMake, and Node.js.
2. Build the native toolchain:

```powershell
.\tools\build.ps1
```

3. Run a quick CLI check:

```powershell
.\tools\bin\hosc.exe version
.\tools\bin\hosc.exe check .\framework\examples\Hello.hosc
```

4. Build editor tooling if your change touches `lsp/` or `vscode-extension/`:

```powershell
Push-Location lsp
npm install
npm run build
Pop-Location

Push-Location vscode-extension
npm install
npm run build
Pop-Location
```

## Branches and Pull Requests

- Keep one logical change per branch.
- Explain the motivation, affected modules, and verification steps in the PR.
- Include screenshots only for framework or editor UI changes.
- Update docs when commands, public APIs, examples, or supported syntax change.

## Code Style

- Native code uses C11.
- Follow existing C naming: `snake_case` functions and fields, `PascalCase`
  typedefs where already used.
- Keep allocation ownership explicit and pair public constructors/destructors.
- Avoid broad rewrites unless the module boundary is already being changed.
- TypeScript code should follow the existing `lsp/` and `vscode-extension/`
  style.

## Testing

Use the most relevant verification path for your change:

```powershell
.\tools\build.ps1 -RunTests
```

```powershell
cmake -S . -B build/cmake -G Ninja
cmake --build build/cmake
ctest --test-dir build/cmake --output-on-failure
```

```powershell
Push-Location lsp
npm run build
Pop-Location
```

```powershell
Push-Location vscode-extension
npm run build
Pop-Location
```

## Documentation Rules

- Do not document aspirational behavior as current behavior.
- If a feature is planned but not implemented, place it in `ROADMAP.md`.
- When older docs disagree with code, update `docs/README.md` trust order or
  add a note in `docs/troubleshooting.md`.

## Reporting Issues

Include:

- HOSC command and full arguments
- Source file or minimal reproduction
- Expected result
- Actual output
- Operating system and compiler version

## License

By contributing, you agree that your contribution is licensed under the Apache
License 2.0.
