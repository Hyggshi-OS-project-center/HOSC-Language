# HOSC Semantics

Level A bootstrap semantics are intentionally narrow:

- `main` is the required entry function.
- `print("...")` is lowered to a native runtime call.
- Diagnostics are reported through `HDiagnosticBag`.

The target long-term semantics remain the structured design documented in the architecture plan.
