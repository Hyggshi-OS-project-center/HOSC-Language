# How To Run Framework

## 1) Build framework executable

```powershell
gcc -Wall -Wextra -std=c99 -O2 -Iframework\include -o framework\bin\hosc_framework.exe framework\src\hosc_framework.c framework\src\hosc_runtime.c framework\src\hosc_modules.c -luser32 -lgdi32 -lkernel32
```

Or with make:

```bash
make -f framework/Makefile.framework framework
```

## 2) Run a GUI demo

```powershell
.\framework\bin\hosc_framework.exe run .\framework\examples\window.hosc
```

Close the window to end `loop()`.

## 3) Run a finite smoke test

```powershell
.\framework\bin\hosc_framework.exe run .\framework\examples\smoke.hosc
```

## 4) Expected syntax

Supported statements:

```hosc
window("Title")
text(20, 20, "Message")
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
    window("HOSC Window")
    text(20, 20, "Hello")
    loop()
}
```

## 5) Troubleshooting

- `gcc not found`: install MinGW GCC and add to PATH.
- `make not found`: use direct gcc build command above.
- GUI not opening: framework falls back to console backend and prints `[GUI:console] ...` lines.