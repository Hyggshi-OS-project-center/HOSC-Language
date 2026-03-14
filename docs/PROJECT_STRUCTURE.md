# HOSC Project Structure

Updated structure aligned with the HOSC refactor (core + compiler + runtime + services):

```
hosc-language/
├── compiler/
│   ├── include/
│   └── src/
├── core/
│   ├── include/
│   └── src/
├── runtime/
│   ├── include/
│   └── src/
├── services/
│   ├── include/
│   └── src/
├── framework/
│   ├── docs/
│   ├── examples/
│   ├── include/          # legacy API header (deprecated)
│   ├── legacy/           # old framework runtime (not built)
│   ├── src/              # framework entry (compiler + HVM pipeline)
│   └── makefile
├── ide/
│   └── vscode_hosc/
├── tools/
│   ├── bin/
│   ├── legacy/
│   ├── makefile
│   ├── build.ps1
│   └── quality_gate.ps1
├── docs/
├── readme.md
└── license.txt
```

Notes:

- GUI functionality is provided through runtime services in `services/`.
- The VM core (`runtime/`) is platform-independent.
- Legacy framework runtime sources are isolated in `framework/legacy`.
