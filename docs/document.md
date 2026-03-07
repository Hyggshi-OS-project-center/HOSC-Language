# HOSC Language Documentation

## Overview
HOSC is a simple programming language designed for learning compiler construction. It features basic variable declarations, expressions, and statements.

## Syntax

### Variable Declarations
```hosc
let identifier = expression;
```

Example:
```hosc
let x = 10;
let y = 20 + x;
```

### Data Types
- Numbers (integers)
- Strings (planned)
- Booleans (planned)

### Expressions
- Numeric literals: `42`, `100`, `0`
- Variables: `x`, `y`, `counter`
- Arithmetic operations (planned):
  - Addition: `a + b`
  - Subtraction: `a - b`
  - Multiplication: `a * b`
  - Division: `a / b`

### Statements
- Variable declarations
- Expression statements (planned)
- Control flow (planned):
  - if/else
  - while loops

## Example Programs

Basic variable declaration:
```hosc
let x = 42;
```

Multiple declarations (planned):
```hosc
let a = 10;
let b = 20;
let sum = a + b;
```

## Compilation

HOSC programs are compiled to C code. The compiler performs:
1. Lexical analysis (tokenization)
2. Parsing (AST construction)
3. Code generation (C output)

## Error Handling

The compiler reports:
- Syntax errors
- Undefined variables
- Type mismatches

## Implementation Status

Current features:
- [x] Basic lexer
- [x] Basic parser
- [x] Simple code generation
- [x] Variable declarations
- [x] Number literals

Planned features:
- [ ] Arithmetic expressions
- [ ] Control flow statements
- [ ] String support
- [ ] Type checking
- [ ] Error recovery