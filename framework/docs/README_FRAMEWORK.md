# HOSC Framework

`framework/` is a standalone runtime layer for GUI/event demos.
It is separate from the main `hosc -> bytecode -> hvm` pipeline, and is useful for testing framework-level APIs quickly.

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

### Windows (PowerShell, GCC in PATH)

```powershell
gcc -Wall -Wextra -std=c99 -O2 -Iframework\include -o framework\bin\hosc_framework.exe framework\src\hosc_framework.c framework\src\hosc_runtime.c framework\src\hosc_modules.c -luser32 -lgdi32 -lkernel32
```

### Makefile

```bash
make -f framework/Makefile.framework framework
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
- For production language flow, prefer `tools/bin/hosc.exe run <file.hosc>` (compiler + HVM path).