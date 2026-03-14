# HOSC Framework

`framework/` is a GUI demo runner built on the main `hosc -> bytecode -> hvm` pipeline.
It uses runtime services for GUI calls so the VM stays platform-independent.

## What it supports

- `window("title")`
- `text(x, y, "message")`
- `pump_events()`
- `on_click(x, y, "message")`
- `on_key(key, x, y, "message")`
- `on_mouse_move(x, y, "message")`
- `loop()` and `loop(frames, sleep_ms)`
- `loop(frames, sleep_ms) { ... }` block syntax (legacy-compatible)

## Modern HOSC wrapper support

Framework parser now accepts wrappers:

```hosc
package main

func main() {
    window("HOSC Window")
    text(20, 20, "Hello")
    loop()
}
```

## Build

### Makefile

```bash
make -f framework/makefile framework
```

## Run

```powershell
.\framework\bin\hosc_framework.exe run .\framework\examples\window.hosc
```

Quick non-blocking smoke example:

```powershell
.\framework\bin\hosc_framework.exe run .\framework\examples\smoke.hosc
```

## Notes

- Framework GUI uses Win32 backend when available, with console fallback.
- Legacy framework runtime sources live under `framework/legacy` and are not built by default.
- For production language flow, prefer `tools/bin/hosc.exe run <file.hosc>` (compiler + HVM path).
