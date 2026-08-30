# HOSC Bootstrap Compiler (Compiler 0)

The `bootstrap/` directory contains **HOSC Compiler 0** — a compiler and interpreter written in pure C, serving as the first stage in HOSC's self-hosting roadmap.

## Architecture

```
HOSC Source (.hosc)
        │
        ▼
  ┌───────────┐
  │   Lexer   │  ← Stable, deterministic (never changes when new commands are added)
  └─────┬─────┘
        │
        ▼
  ┌───────────┐
  │  Parser   │  ← Stable core grammar (recursive-descent)
  └─────┬─────┘
        │
        ▼
      AST
        │
   ┌────┴────────────────┐
   ▼                     ▼
Command Registry    AST Rewriter        ← All extensibility lives here
   │                (Macro Expander)
   └────────┬───────────┘
            │
       ┌────┴────┐
       ▼         ▼
  Interpreter  Bytecode VM
 (Tree-walk)  (Stack-based)
```

### Core Design Principle

> **Stable core grammar — Extensions live at the AST layer**
>
> The Lexer and Parser are **never modified** when users define new commands.
> All extensibility happens after the AST has been built, through
> `CommandRegistry` and `AstRewriter`. This keeps the compiler debuggable
> and ready for self-hosting.

---

## Command Registry

```c
typedef enum {
    COMMAND_AST,     // Behavioral extension — runs AST body at runtime
    COMMAND_NATIVE,  // Native C callback — registered via C API
    COMMAND_MACRO    // Syntax extension   — rewrites AST before evaluation
} CommandKind;

typedef struct {
    const char *name;
    size_t      parameter_count;
    CommandKind kind;
    ASTNode        *body;   // Used by COMMAND_AST and COMMAND_MACRO
    BsNativeCommand native; // Used by COMMAND_NATIVE
} BsCommand;
```

---

## Extensibility: Three Mechanisms

### 1. `command` — Behavioral Extension (AST Command)

Extends **behavior** — like a named function, but registered in `CommandRegistry`:

```hosc
command log_info(message) {
    print("[INFO] " + message);
}

// Called like a natural-language keyword:
log_info("System started");
```

### 2. `macro` / `syntax` — Syntax Extension (AST Transformation)

Extends **syntax** through AST transformation performed *before* evaluation.
The core grammar is never touched:

```hosc
// `unless` expands to: if (!condition) { ... }
macro unless(condition) {
    if (!condition) {
        print("condition was false");
    }
}

// `repeat` expands to a while loop
macro repeat(n) {
    var __i = 0;
    while (__i < n) {
        print("iteration " + __i);
        __i = __i + 1;
    }
}

// `syntax` is an alias for `macro`
syntax debug_val(val) {
    print("[DEBUG] " + val);
}
```

Expansion pipeline:

```
syntax unless(...)
        ↓
Macro expansion (AST Rewriter)
        ↓
if (!condition) { ... }
        ↓
AST → Interpreter / VM
```

### 3. C API — Native Command Registration

```c
// Define a C callback:
static BsValue my_make_window(BsRuntime *runtime, BsValue *args, size_t argc) {
    printf("Creating window: %s\n", args[0].as.string);
    return bs_null();
}

// Register into the runtime:
bootstrap_register_command(
    runtime,
    "callwindow",   // name as it appears in HOSC source
    1,              // parameter count
    my_make_window
);
```

The compiler then understands `callwindow` automatically — no hard-coding needed:

```hosc
callwindow("My Window");
```

---

## Self-Hosting Roadmap

```
C Bootstrap  (Compiler 0)    ← this directory
        │
        ▼
HOSC Compiler 1              ← written in HOSC, runs on Compiler 0
        │
        ▼
HOSC Compiler 2              ← Compiler 1 compiles its own source
        │
        ▼
HOSC 3.x  Self-hosted        ← no longer depends on the C bootstrap
```

The original C bootstrap remains in project history, but the canonical
compiler will eventually be written and compiled entirely in HOSC.

---

## Build

```bash
# Build the entire project (includes bootstrap):
cmake -B build && cmake --build build

# Build only the bootstrap targets:
cmake --build build --target hosc-bootstrap bootstrap_tests
```

## Usage

```bash
# Run a .hosc file (tree-walk interpreter — recommended):
./build/bin/Debug/hosc-bootstrap run  bootstrap/examples/01_basic.hosc

# Compile to bytecode and run in the stack VM:
./build/bin/Debug/hosc-bootstrap vm   bootstrap/examples/01_basic.hosc

# Parse and dump the AST:
./build/bin/Debug/hosc-bootstrap ast  bootstrap/examples/02_custom_commands.hosc

# Compile to bytecode and disassemble:
./build/bin/Debug/hosc-bootstrap disasm bootstrap/examples/01_basic.hosc

# Start the interactive REPL:
./build/bin/Debug/hosc-bootstrap repl
```

### REPL special commands

| Command | Description |
|---------|-------------|
| `:cmds` | List all registered commands and macros |
| `:help` | Show usage help |
| `exit` / `quit` | Exit the REPL |

---

## Tests

```bash
# Run the test suite directly:
./build/bin/Debug/bootstrap_tests

# Or via CTest:
ctest --test-dir build --output-on-failure -R bootstrap
```

**57/57 tests passing** across: Lexer, Parser, Command Registry, Interpreter, C API.

---

## Directory Layout

```
bootstrap/
├── include/
│   ├── bootstrap.h             # Master include + high-level run API
│   ├── bs_value.h              # Dynamic values: null, bool, int, float, string, fn
│   ├── bs_token.h              # Token type definitions
│   ├── bs_lexer.h              # Lexer interface
│   ├── bs_ast.h                # AST node types and constructors
│   ├── bs_command_registry.h   # Command registry (AST / NATIVE / MACRO)
│   ├── bs_ast_rewriter.h       # Macro expander and AST rewrite pass
│   ├── bs_parser.h             # Recursive-descent parser
│   ├── bs_runtime.h            # Runtime environment + C API
│   ├── bs_interpreter.h        # Tree-walk interpreter
│   ├── bs_bytecode.h           # Bytecode opcodes and chunk
│   ├── bs_codegen.h            # AST → Bytecode compiler
│   ├── bs_vm.h                 # Stack-based bytecode VM
│   └── bs_repl.h               # Interactive REPL
├── src/
│   ├── bs_value.c
│   ├── bs_lexer.c
│   ├── bs_ast.c
│   ├── bs_command_registry.c
│   ├── bs_ast_rewriter.c
│   ├── bs_parser.c
│   ├── bs_runtime.c            # Built-ins: clock, log, assert_eq, callwindow
│   ├── bs_interpreter.c        # Tree-walk evaluator
│   ├── bs_bytecode.c           # Chunk ops and disassembly
│   ├── bs_codegen.c            # AST-to-bytecode compiler
│   ├── bs_vm.c                 # Bytecode VM execution loop
│   ├── bs_repl.c               # Interactive REPL
│   ├── bootstrap.c             # High-level bs_run_* API
│   └── main.c                  # CLI: run / vm / ast / disasm / repl
├── examples/
│   ├── 01_basic.hosc           # Core language features
│   ├── 02_custom_commands.hosc # command / macro / syntax / C API demo
│   └── 03_dsl_builder.hosc     # DSL example: test framework + config DSL
├── tests/
│   └── test_bootstrap.c        # 57 unit and integration tests
├── CMakeLists.txt
└── README.md
```

---

## Extension Mechanism Comparison

| Mechanism | Keyword | Phase | Primary use |
|-----------|---------|-------|-------------|
| AST command | `command` | Runtime | Add reusable behavior (`log_info`, `swap`) |
| Syntax macro | `macro` / `syntax` | AST rewrite | Add new syntax (`unless`, `repeat`) |
| Native C API | `bootstrap_register_command()` | Runtime | Integrate system APIs (`callwindow`) |

All three mechanisms share the same `CommandRegistry` — the compiler dispatches
through it uniformly regardless of `CommandKind`.
