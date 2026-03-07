# HOSC VS Code Extension

Folder: `ide-extension/vscode-hosc`

## New capabilities

- Debugger in VS Code (`hosc` debug type + `Debug HOSC File` command)
- Autocomplete + IntelliSense (completion, hover; fallback provider)
- LSP server (completion, hover, signature help, symbols, definition, diagnostics)

## Commands

- `hosc.runFile`
- `hosc.build`
- `hosc.check`
- `hosc.debugFile`

## Debugger usage

1. Open a `.hosc` file.
2. Run command `Debug HOSC File`.
3. Or create launch config:

```json
{
  "type": "hosc",
  "request": "launch",
  "name": "Debug Current HOSC File",
  "program": "${file}",
  "cwd": "${workspaceFolder}",
  "stopOnEntry": false
}
```

## Install dependencies (required for LSP)

```powershell
cd ide-extension/vscode-hosc
npm install
```

If you skip `npm install`, extension still has fallback completion/hover, but full LSP features will not start.

## Settings

- `hosc.cliPath`
- `hosc.runOnSaveCheck`
- `hosc.lsp.enabled`
- `hosc.debug.stopOnEntry`

## Run extension locally

1. Open `ide-extension/vscode-hosc` in VS Code.
2. Press `F5` to launch Extension Development Host.
3. Open a `.hosc` file and test Run/Build/Debug.