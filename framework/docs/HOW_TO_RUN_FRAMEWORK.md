# How To Run Framework

## 1) Build framework executable

```powershell
gcc -Wall -Wextra -std=c99 -O2 -Iframework\include -o framework\bin\hosc_framework.exe framework\src\hosc_framework.c framework\src\hosc_runtime.c framework\src\hosc_modules.c -luser32 -lgdi32 -lkernel32 -lwinmm -lgdiplus -lole32 -lshell32 -lcomdlg32 -lmfplay -lmfplat -lmf -lmfuuid
```

Or with make:

```bash
make -f framework/Makefile.framework framework
```

## 2) Run a GUI demo

```powershell
.\framework\bin\hosc_framework.exe run .\framework\examples\hello_world_window.hosc
```

Close the window to end `loop()`.

Self-contained hello world example:

```powershell
.\framework\bin\hosc_framework.exe run .\framework\examples\hello_world_window.hosc
```

## 3) Run the simple hello example

```powershell
.\framework\bin\hosc_framework.exe run .\framework\examples\Hello.hosc
```

## 4) Expected syntax

Supported statements:

```hosc
window({
    title: "Title",
    width: 380,
    height: 465,
    resizable: false,
    fullscreen: false,
    icon: "calc.png",
    minWidth: 300,
    minHeight: 300,
    center: true
})
window("Title")
text({ x: 20, y: 20, content: "Message" })
text({ x: 20, y: 20, w: 400, h: 200, content: "Message" })  <!-- h = font size -->
text(20, 20, "Message")
image(20, 50, 320, 320, "asset.png")
image({
    x: 20,
    y: 50,
    width: 320,
    height: 320,
    src: "asset.png"
})
audio.play({ src: "asset.mp3" })
play_sound("asset.mp3")
system.messageBox({ title: "HOSC", content: "Done", icon: "info" })
win32_message_box("Done")
pump_events()
on_click(20, 60, "clicked")
on_key(-1, 20, 90, "key")
on_mouse_move(20, 120, "moving")
loop()
loop(120, 16)
loop(0, 16) {
    on_click(20, 60, "clicked")
}
```

Wrapper syntax is accepted:

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

Another runnable example lives at `framework/examples/hello_world_window.hosc`. It opens a window, shows a hello-world message box, and draws a miniature copy of its own source text into the window.

## 5) Troubleshooting

- `gcc not found`: install MinGW GCC and add to PATH.
- `make not found`: use direct gcc build command above.
- GUI not opening: framework falls back to console backend and prints `[GUI:console] ...` lines.

## 6) From Luau/Python to HOSC (quick onboarding)

```luau
print("Hello")
if x > 5 then
    print("OK")
end
```

```python
print("Hello")
if x > 5:
    print("OK")
```

```hosc
print("Hello")
if x > 5 {
    print("OK")
}
```

GUI call style in HOSC supports both concise and named form:

```hosc
image(120, 80, 400, 400, "momoi.png")
image({
    x: 120,
    y: 80,
    width: 400,
    height: 400,
    src: "momoi.png"
})
```

Message/audio style:

```hosc
system.messageBox({ title: "Hi", content: "Hello", icon: "info" })
audio.play({ src: "theme.mp3" })
```
