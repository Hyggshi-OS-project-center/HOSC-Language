# HOSC Framework

`framework/` is a standalone runtime layer for GUI/event demos.
It is separate from the main `hosc -> bytecode -> hvm` pipeline, and is useful for testing framework-level APIs quickly.

## What it supports

- `window({ ... })`
- `window("title")` legacy compatibility
- `text({ x, y, content })`
- `text(x, y, "message")` legacy compatibility
- `image(x, y, width, height, "path")`
- `image({ x, y, width, height, src })`
- `audio.play({ src, loop?, volume? })`
- `play_sound("path.mp3")` legacy compatibility
- `system.messageBox({ title?, content, icon? })`
- `win32_message_box("message")` legacy compatibility
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
    window({
        title: "HOSC Window",
        width: 380,
        height: 465,
        resizable: false,
        fullscreen: false,
        icon: "calc.png",
        minWidth: 300,
        minHeight: 300,
        center: true
    })
    text(20, 20, "Hello")
    loop()
}
```

Supported `window({ ... })` properties:

- `title`
- `width`
- `height`
- `resizable`
- `fullscreen`
- `icon`
- `minWidth`
- `minHeight`
- `center`

Supported `image({ ... })` properties:

- `x`
- `y`
- `width`
- `height`
- `src`

Quick reference:

- `text({ x: 20, y: 20, content: "message" })`
- `text(x, y, "message")` (legacy)
- `image(x, y, width, height, "path")`
- `image({ x: 20, y: 50, width: 320, height: 320, src: "asset.png" })`
- `audio.play({ src: "asset.mp3" })`
- `system.messageBox({ title: "HOSC", content: "Done", icon: "info" })`

## Build

### Windows (PowerShell, GCC in PATH)

```powershell
gcc -Wall -Wextra -std=c99 -O2 -Iframework\include -o framework\bin\hosc_framework.exe framework\src\hosc_framework.c framework\src\hosc_runtime.c framework\src\hosc_modules.c -luser32 -lgdi32 -lkernel32 -lwinmm -lgdiplus -lole32 -lshell32 -lcomdlg32 -lmfplay -lmfplat -lmf -lmfuuid
```

### Makefile

```bash
make -f framework/Makefile.framework framework
```

## Run

```powershell
.\framework\bin\hosc_framework.exe run .\framework\examples\hello_world_window.hosc
```

Hello-world self-window example:

```powershell
.\framework\bin\hosc_framework.exe run .\framework\examples\hello_world_window.hosc
```

Current non-window hello example:

```powershell
.\framework\bin\hosc_framework.exe run .\framework\examples\Hello.hosc
```

## Notes

- Framework GUI uses Win32 backend when available, with console fallback.
- For production language flow, prefer `tools/bin/hosc.exe run <file.hosc>` (compiler + HVM path).
- `framework/examples/hello_world_window.hosc` is a small GUI demo that displays a hello-world message and draws a compact copy of its own source inside the window.
