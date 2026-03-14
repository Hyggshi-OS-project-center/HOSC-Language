# HVM Runtime Overview

The HOSC Virtual Machine (HVM) executes bytecode produced by the compiler pipeline:

1. Parse source into AST
2. Compile AST to HVM bytecode
3. Execute bytecode via the HVM interpreter

The runtime is platform-independent and calls external capabilities (GUI, timers, I/O)
through the runtime services interface. GUI functionality is implemented as a service
plugin so the core VM remains portable.
