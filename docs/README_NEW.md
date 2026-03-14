# HOSC Language - Multiple Implementation Approaches

The HOSC (Hyggshi OS Code) language now supports multiple implementation approaches, giving you flexibility in how you use the language.

## 🚀 Available Implementations

### 1. CLI Implementation (`hosc-cli.exe`)
The original command-line interface implementation.

**Usage:**
```bash
hosc-cli.exe run <file.hosc>     # Generate C code
hosc-cli.exe exec <file.hosc>    # Execute directly
hosc-cli.exe debug <file.hosc>   # Debug mode
```

**Build:**
```bash
make cli
```

### 2. Standalone Engine (`hosc_engine.exe`)
A standalone HOSC language engine that doesn't require CLI commands.

**Features:**
- Built-in demo and examples
- Direct program execution
- Code compilation to C
- No command-line arguments needed

**Usage:**
```bash
hosc_engine.exe  # Runs comprehensive demo
```

**Build:**
```bash
make engine
```

### 3. Shared Library (`hosc_lib.dll`)
A shared library that can be used by other programs.

**API:**
```c
#include "hosc_lib.h"

// Initialize context
HOSCContext *ctx = hosc_init();

// Execute HOSC code
hosc_execute(ctx, "println(\"Hello HOSC!\")");

// Compile to C
char *c_code = hosc_compile(ctx, "var x = 42");

// Quick functions (no context needed)
hosc_quick_execute("debug_print(\"Quick execution\")");
char *code = hosc_quick_compile("error(\"Error message\")");

// Cleanup
hosc_cleanup(ctx);
```

**Build:**
```bash
make library
```

### 4. Library Example (`hosc_example.exe`)
An example program showing how to use the HOSC library.

**Usage:**
```bash
hosc_example.exe  # Demonstrates library usage
```

**Build:**
```bash
make example
```

## 🛠️ Build System

Use the new Makefile for easy building:

```bash
# Build all implementations
make all

# Build specific implementation
make cli
make engine
make library
make example

# Debug builds
make debug-cli
make debug-engine

# Test implementations
make test-all
make test-cli
make test-engine
make test-library

# Clean build artifacts
make clean

# Show help
make help
```

## 📋 HOSC Language Features

### Language Structure
- `package` declarations
- `import` statements
- `language:`, `version:`, `extension:`, `features:` declarations

### Type System
- `type`, `struct`, `interface` declarations
- Type annotations and generic parameters
- Optional, Array, Dictionary, Set types

### Variable & Function Declarations
- `var` and `const` declarations
- `func` declarations with parameters and return types
- Support for mutable/immutable variables

### Control Flow
- `if`, `else`, `for`, `while`, `do-while` loops
- `switch`, `case`, `break`, `continue` statements
- Exception handling with `try`, `catch`, `throw`

### Object-Oriented Features
- `self` and `super` references
- Member access and method calls

### Debugging & Development
- `debug`, `trace`, `log`, `assert` statements
- `breakpoint`, `step`, `step_over`, `step_into`, `step_out`
- `continue_execution`, `stop_execution`
- `watch`, `inspect`, `profile`, `benchmark`

### Print Functions
- `print`, `println` - Basic output
- `debug_print`, `trace_print`, `log_print` - Debug output
- `error_print`, `warning_print`, `info_print` - Leveled output
- `debug_break`, `trace_stack`, `log_stack` - Debug utilities

### Literals & Types
- `Int`, `Double`, `Float`, `Bool`, `String`, `Char`
- `Array`, `Dictionary`, `Set` literals
- Type checking and casting

### Operators
- Binary, unary, assignment, comparison, logical, arithmetic operators

### Legacy Win32 Support
- `error()`, `info()`, `warn()`, `ask()`, `msg()`, `sleep()`, `window()`
- File dialogs, color dialogs, font dialogs
- System functions, clipboard, cursor control, etc.

## 📁 Example Files

- `examples/var_decl.hosc` - Variable declaration
- `examples/func_decl.hosc` - Function declaration
- `examples/debug_print.hosc` - Debug printing
- `examples/simple_demo.hosc` - Basic demo
- `examples/comprehensive_demo.hosc` - Full language demo

## 🎯 Use Cases

### CLI Implementation
- Best for: Scripting, automation, batch processing
- Use when: You need command-line interface
- Example: `hosc-cli.exe run script.hosc > output.c`

### Standalone Engine
- Best for: Interactive demos, learning, testing
- Use when: You want everything in one executable
- Example: `hosc_engine.exe` (runs comprehensive demo)

### Shared Library
- Best for: Integration with other applications
- Use when: You want to embed HOSC in your programs
- Example: Use `hosc_lib.h` in your C/C++ projects

## 🔧 Development

All implementations share the same core components:
- `src/ast.h` - Abstract Syntax Tree definitions
- `src/parser.c` - HOSC language parser
- `src/codegen.c` - C code generator
- `src/runtime/executor.c` - Runtime execution engine

The different implementations provide different interfaces to the same powerful HOSC language engine.

## License

This project is part of the HOSC (Hyggshi OS Code) language implementation.
