# HOSC Language Debug Extension

This extension provides debugging support for the HOSC programming language in Visual Studio Code.

## Features

### Debugging
- Debug HOSC programs directly in VS Code
- Set breakpoints in `.hosc` files
- Step through code execution
- Inspect variables and call stack
- Support for launch configurations

### Language support
- 💡 **Auto-completion** for all 22 real keywords (`func`, `if`, `while`, `for`, `switch`, `print`, `prints`, ...) plus the one documented dotted-call built-in, `audio.play`
- 📝 **Hover documentation** for keywords — including the mutability rule (`var` is mutable, `let`/`const` are not — the reverse of JS) and the `prints[...]` raw-string construct
- ⚠️ **Real diagnostics** — runs the actual `hosc check <file>` compiler and surfaces its real `H000`–`H900` output directly in the editor (see "Diagnostics setup" below). This extension does **not** guess at syntax errors client-side.
- 🔧 **Go to Definition** for `func` (current file, plus other `.hosc` files in the workspace)
- 📑 **Outline** listing `func` declarations (this grammar has no `struct`/`interface`)
- 🎨 **Semantic highlighting** distinguishing function names from `var`-declared (mutable) names
- 🛠️ **Formatter** (`Format Document`) — brace-based reindentation that leaves `prints[` raw-string/ASCII-art content completely untouched. This fills the gap while the real `hosc fmt` is a bootstrap stub.
- ✨ **Snippets** — type `main` and press Tab to generate:
  ```hosc
  package main

  func main() {

  }
  ```
  Other snippets: `func`, `if`, `ifelse`, `for` (C-style), `forcond` (bare-condition), `while`, `switch`, `print`, `prints`, `import`.

### Diagnostics setup

Real diagnostics require a built `hosc` executable. The extension looks for it in this order:
1. The `hosc.executablePath` setting (absolute, or relative to the workspace folder)
2. `${workspaceFolder}/tools/bin/hosc.exe` / `hosc` (where `tools/build.ps1` places it)
3. `hosc` / `hosc.exe` on your `PATH`

If none of those resolve, diagnostics are simply disabled (check the **HOSC** output channel for a one-time note) rather than falling back to guessed errors.

> **Why not a client-side linter?** An earlier version of this extension tried to emulate the compiler's `H104`/`H205` diagnostics with regexes and got both the meaning of those codes and basic grammar facts (optional semicolons, `prints[...]` syntax, paren-optional `if`/`while`) wrong — producing false-positive squiggles on valid code. Shelling out to the real compiler (the same approach the project's own `lsp/` already uses) avoids that class of bug entirely.

## Prerequisites

- VS Code 1.80.0 or higher
- HOSC compiler and runtime built and available in PATH
- Node.js 20+ (for building the extension)

## Building the Extension

```bash
cd vscode-extension
npm install
npm run compile
```

## Running the Extension

1. Open the `vscode-extension` folder in VS Code
2. Press `F5` to launch a new Extension Development Host window
3. In the new window, open a HOSC project
4. Create a `.vscode/launch.json` file with the following configuration:

```json
{
  "version": "0.2.0",
  "configurations": [
    {
      "type": "hosc",
      "request": "launch",
      "name": "Debug HOSC Program",
      "program": "${workspaceFolder}/hello.hosc",
      "stopOnEntry": false
    }
  ]
}
```

5. Press `F5` to start debugging

## Debug Configuration

The extension supports the following debug configuration options:

- `program` (required): Path to the HOSC program to debug
- `stopOnEntry` (optional): Automatically stop after launch (default: false)
- `trace` (optional): Enable logging of the debug adapter protocol (default: false)

## Extension Structure

```
vscode-extension/
├── src/
│   ├── extension.ts          # Extension entry point
│   └── debugAdapter.ts       # Debug adapter implementation
├── .vscode/
│   ├── launch.json           # Extension development launch config
│   └── tasks.json            # Build tasks
├── package.json              # Extension manifest
├── tsconfig.json             # TypeScript configuration
└── README.md                 # This file
```

## Debug Adapter Protocol

This extension implements the Debug Adapter Protocol (DAP) to communicate between VS Code and the HOSC runtime. The debug adapter:

- Launches HOSC programs
- Manages breakpoints
- Provides stack traces
- Exposes variable scopes
- Supports stepping operations

## Known Limitations

- This is a bootstrap implementation with basic debugging support
- Advanced features like conditional breakpoints, hit counts, and log points are not yet supported
- Variable inspection is currently limited
- Step debugging is simulated (full implementation requires HOSC VM debug hooks)

## Contributing

See the main [HOSC Language repository](https://github.com/Hyggshi-OS-project-center/HOSC-Language) for contribution guidelines.

## License

Apache License 2.0 - See LICENSE file for details.