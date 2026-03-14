# HOSC Language Syntax Reference

**Version 1.0** | Complete Syntax Specification

---

## Table of Contents

1. [Language Overview](#language-overview)
2. [Lexical Elements](#lexical-elements)
3. [Data Types](#data-types)
4. [Variables and Constants](#variables-and-constants)
5. [Expressions](#expressions)
6. [Statements](#statements)
7. [Functions](#functions)
8. [Control Flow](#control-flow)
9. [Built-in Functions](#built-in-functions)
10. [Comments](#comments)
11. [Examples](#examples)

---

## Language Overview

HOSC (High-level Operating System Control) is a domain-specific language designed for system-level programming and Windows API integration. It provides a clean, readable syntax for common programming tasks while maintaining direct access to system capabilities.

### Key Features

- **Simple, readable syntax** inspired by modern languages
- **Type inference** for automatic type detection
- **Windows API integration** with built-in system functions
- **Portable execution** via HOSC Virtual Machine (HVM)
- **Memory safety** with automatic memory management

---

## Lexical Elements

### Identifiers

Identifiers are used for variable names, function names, and other user-defined symbols.

**Rules:**
- Must start with a letter (`a-z`, `A-Z`) or underscore (`_`)
- Can contain letters, digits (`0-9`), and underscores
- Case-sensitive
- Cannot be a reserved keyword

**Examples:**
```
myVariable
_private
counter123
MAX_SIZE
```

### Keywords

Reserved words that have special meaning in the language:

```
let          const        function     return
if           else        while        for
break        continue     true         false
null         import       package      as
print        println      debug_print
error        warning      info
```

### Operators

#### Arithmetic Operators
```
+    Addition
-    Subtraction
*    Multiplication
/    Division
%    Modulo
```

#### Comparison Operators
```
==   Equal to
!=   Not equal to
<    Less than
<=   Less than or equal to
>    Greater than
>=   Greater than or equal to
```

#### Logical Operators
```
&&   Logical AND
||   Logical OR
!    Logical NOT
```

#### Assignment Operators
```
=    Assignment
+=   Addition assignment
-=   Subtraction assignment
*=   Multiplication assignment
/=   Division assignment
%=   Modulo assignment
```

#### Other Operators
```
()   Function call / Grouping
[]   Array indexing
.    Member access
,    Separator
;    Statement terminator
```

### Literals

#### Integer Literals
```
42
-10
0
1000
```

#### Floating-Point Literals
```
3.14
-0.5
2.71828
1.0
```

#### String Literals
```
"Hello, World!"
"Multi-line
string"
"Escaped \"quotes\""
```

#### Boolean Literals
```
true
false
```

#### Null Literal
```
null
```

---

## Data Types

HOSC supports the following data types:

### Primitive Types

1. **Integer** (`int`)
   - 64-bit signed integer
   - Range: -9,223,372,036,854,775,808 to 9,223,372,036,854,775,807

2. **Float** (`float`)
   - 64-bit double-precision floating-point
   - IEEE 754 standard

3. **String** (`string`)
   - Sequence of characters
   - Immutable
   - UTF-8 encoded

4. **Boolean** (`bool`)
   - `true` or `false`

5. **Null** (`null`)
   - Represents absence of value

### Type Inference

HOSC automatically infers types from values:

```hosc
let x = 42;           // int
let y = 3.14;         // float
let name = "HOSC";    // string
let active = true;    // bool
```

---

## Variables and Constants

### Variable Declaration

Variables are declared using the `let` keyword:

**Syntax:**
```
let identifier = expression;
```

**Examples:**
```hosc
let count = 0;
let price = 99.99;
let message = "Hello";
let isActive = true;
```

### Constant Declaration

Constants are declared using the `const` keyword and cannot be reassigned:

**Syntax:**
```
const identifier = expression;
```

**Examples:**
```hosc
const PI = 3.14159;
const MAX_USERS = 1000;
const APP_NAME = "HOSC Application";
```

### Variable Assignment

Variables can be reassigned using the assignment operator:

```hosc
let x = 10;
x = 20;              // Reassign x to 20
x += 5;              // x is now 25
x *= 2;              // x is now 50
```

---

## Expressions

### Arithmetic Expressions

```hosc
let sum = 10 + 20;           // 30
let diff = 100 - 50;         // 50
let product = 5 * 6;          // 30
let quotient = 20 / 4;        // 5
let remainder = 17 % 5;       // 2
```

### Comparison Expressions

```hosc
let isEqual = (x == y);
let isNotEqual = (x != y);
let isLess = (x < y);
let isGreater = (x > y);
```

### Logical Expressions

```hosc
let both = (x > 0 && y > 0);
let either = (x > 0 || y > 0);
let not = !isActive;
```

### String Concatenation

```hosc
let greeting = "Hello" + " " + "World";  // "Hello World"
let fullName = firstName + " " + lastName;
```

---

## Statements

### Expression Statements

Any expression followed by a semicolon is a statement:

```hosc
x + y;
print "Hello";
```

### Variable Declaration Statement

```hosc
let x = 10;
let name = "HOSC";
```

### Print Statements

#### Basic Print
```hosc
print "Hello, World!";
print x;
print "Value: " + x;
```

#### Print Line
```hosc
println "Hello, World!";
println x;
```

#### Debug Print
```hosc
debug_print "Debug information";
debug_print variable;
```

#### Error Print
```hosc
error "An error occurred!";
```

#### Warning Print
```hosc
warning "This is a warning!";
```

#### Info Print
```hosc
info "Information message";
```

---

## Functions

### Function Declaration

**Syntax:**
```
function functionName(parameter1, parameter2, ...) {
    // function body
    return value;
}
```

**Examples:**
```hosc
function add(a, b) {
    return a + b;
}

function greet(name) {
    return "Hello, " + name + "!";
}

function max(a, b) {
    if (a > b) {
        return a;
    } else {
        return b;
    }
}
```

### Function Call

```hosc
let result = add(5, 3);
let message = greet("HOSC");
let maximum = max(10, 20);
```

### Return Statement

Functions can return values using the `return` statement:

```hosc
function square(x) {
    return x * x;
}

function isEven(n) {
    return n % 2 == 0;
}
```

---

## Control Flow

### If Statement

**Syntax:**
```
if (condition) {
    // statements
} else {
    // statements
}
```

**Examples:**
```hosc
if (x > 0) {
    print "Positive";
} else {
    print "Non-positive";
}

if (age >= 18) {
    print "Adult";
} else if (age >= 13) {
    print "Teenager";
} else {
    print "Child";
}
```

### While Loop

**Syntax:**
```
while (condition) {
    // statements
}
```

**Examples:**
```hosc
let i = 0;
while (i < 10) {
    print i;
    i = i + 1;
}

while (isRunning) {
    process();
}
```

### For Loop

**Syntax:**
```
for (initialization; condition; increment) {
    // statements
}
```

**Examples:**
```hosc
for (let i = 0; i < 10; i = i + 1) {
    print i;
}

for (let i = 0; i < array.length; i = i + 1) {
    print array[i];
}
```

### Break Statement

Exits the innermost loop:

```hosc
while (true) {
    if (condition) {
        break;
    }
}
```

### Continue Statement

Skips to the next iteration of the loop:

```hosc
for (let i = 0; i < 10; i = i + 1) {
    if (i % 2 == 0) {
        continue;
    }
    print i;
}
```

---

## Built-in Functions

### Windows API Functions

#### MessageBox
```hosc
win32_message_box "Hello, World!";
win32_message_box "Title", "Message";
```

#### Error MessageBox
```hosc
win32_error "An error occurred!";
```

#### Info MessageBox
```hosc
win32_info "Information message";
```

#### Warning MessageBox
```hosc
win32_warning "Warning message";
```

#### Yes/No Dialog
```hosc
let result = win32_yesno "Do you want to continue?";
```

#### Create Window
```hosc
win32_create_window "Window Title", "Window Message";
```

### System Functions

#### Sleep
```hosc
sleep 1000;  // Sleep for 1000 milliseconds
```

#### Beep
```hosc
beep 1000;   // Beep at 1000 Hz
```

#### File Dialog
```hosc
let file = win32_file_dialog "Open File", "Text Files (*.txt)|*.txt";
```

#### Color Dialog
```hosc
let color = win32_color_dialog;
```

#### Font Dialog
```hosc
let font = win32_font_dialog;
```

#### Open URL
```hosc
win32_open_url "https://example.com";
```

#### Get Screen Size
```hosc
let size = win32_get_screen_size;
```

#### Get Cursor Position
```hosc
let pos = win32_get_cursor_pos;
```

#### Set Cursor Position
```hosc
win32_set_cursor_pos 100, 200;
```

#### Get Clipboard Text
```hosc
let text = win32_get_clipboard_text;
```

#### Set Clipboard Text
```hosc
win32_set_clipboard_text "Hello, Clipboard!";
```

#### Get System Info
```hosc
let info = win32_get_system_info;
```

#### Get Time
```hosc
let time = win32_get_time;
```

---

## Comments

### Single-Line Comments

```hosc
// This is a single-line comment
let x = 10;  // Comment after code
```

### Multi-Line Comments

```hosc
/*
 * This is a multi-line comment
 * It can span multiple lines
 */
```

---

## Examples

### Example 1: Hello World

```hosc
// Simple Hello World program
print "Hello, World!";
```

### Example 2: Variable Operations

```hosc
let x = 10;
let y = 20;
let sum = x + y;
let product = x * y;

print "Sum: " + sum;
print "Product: " + product;
```

### Example 3: Conditional Logic

```hosc
let age = 25;

if (age >= 18) {
    print "You are an adult";
} else {
    print "You are a minor";
}
```

### Example 4: Loop

```hosc
// Count from 1 to 10
for (let i = 1; i <= 10; i = i + 1) {
    print i;
}
```

### Example 5: Function

```hosc
function factorial(n) {
    if (n <= 1) {
        return 1;
    } else {
        return n * factorial(n - 1);
    }
}

let result = factorial(5);
print "Factorial of 5: " + result;
```

### Example 6: Windows API Integration

```hosc
// Display a message box
win32_message_box "Welcome to HOSC!";

// Get user confirmation
let confirmed = win32_yesno "Do you want to continue?";

if (confirmed) {
    win32_info "You chose to continue!";
} else {
    win32_warning "You chose to cancel.";
}
```

### Example 7: Complete Program

```hosc
// HOSC Program: Calculator
function add(a, b) {
    return a + b;
}

function subtract(a, b) {
    return a - b;
}

function multiply(a, b) {
    return a * b;
}

function divide(a, b) {
    if (b == 0) {
        error "Division by zero!";
        return null;
    }
    return a / b;
}

// Main program
let x = 10;
let y = 5;

print "Calculator Demo";
print "x = " + x;
print "y = " + y;
print "x + y = " + add(x, y);
print "x - y = " + subtract(x, y);
print "x * y = " + multiply(x, y);
print "x / y = " + divide(x, y);
```

---

## Grammar Summary

### EBNF Grammar

```
Program          ::= Statement*
Statement        ::= VariableDecl | ConstDecl | FunctionDecl | ExpressionStmt | 
                     PrintStmt | IfStmt | WhileStmt | ForStmt | ReturnStmt | 
                     BreakStmt | ContinueStmt
VariableDecl     ::= "let" Identifier "=" Expression ";"
ConstDecl        ::= "const" Identifier "=" Expression ";"
FunctionDecl     ::= "function" Identifier "(" ParameterList? ")" Block
ParameterList    ::= Identifier ("," Identifier)*
ExpressionStmt   ::= Expression ";"
PrintStmt        ::= ("print" | "println" | "debug_print" | "error" | "warning" | "info") Expression ";"
IfStmt           ::= "if" "(" Expression ")" Block ("else" Block)?
WhileStmt        ::= "while" "(" Expression ")" Block
ForStmt          ::= "for" "(" VariableDecl? ";" Expression? ";" Expression? ")" Block
ReturnStmt       ::= "return" Expression? ";"
BreakStmt        ::= "break" ";"
ContinueStmt     ::= "continue" ";"
Block            ::= "{" Statement* "}"
Expression       ::= Assignment
Assignment       ::= Identifier ("=" | "+=" | "-=" | "*=" | "/=" | "%=") Assignment | LogicalOr
LogicalOr        ::= LogicalAnd ("||" LogicalAnd)*
LogicalAnd       ::= Equality ("&&" Equality)*
Equality         ::= Comparison (("==" | "!=") Comparison)*
Comparison       ::= Term (("<" | "<=" | ">" | ">=") Term)*
Term             ::= Factor (("+" | "-") Factor)*
Factor           ::= Unary (("*" | "/" | "%") Unary)*
Unary            ::= ("!" | "-") Unary | Primary
Primary          ::= Literal | Identifier | "(" Expression ")" | FunctionCall
FunctionCall     ::= Identifier "(" ArgumentList? ")"
ArgumentList     ::= Expression ("," Expression)*
Literal          ::= Integer | Float | String | Boolean | Null
Identifier       ::= [a-zA-Z_][a-zA-Z0-9_]*
Integer          ::= [0-9]+
Float            ::= [0-9]+ "." [0-9]+
String           ::= '"' ([^"\] | EscapeSequence)* '"'
Boolean          ::= "true" | "false"
Null             ::= "null"
EscapeSequence   ::= "\\" ([nrt"\\] | "u" HexDigit{4})
HexDigit         ::= [0-9a-fA-F]
```

---

## Reserved Keywords

The following words are reserved and cannot be used as identifiers:

```
let          const        function     return
if           else         while        for
break        continue     true         false
null         import       package      as
print        println      debug_print
error        warning      info
win32_message_box
win32_error
win32_info
win32_warning
win32_yesno
win32_create_window
win32_sleep
win32_file_dialog
win32_color_dialog
win32_font_dialog
win32_open_url
win32_beep
win32_get_screen_size
win32_get_cursor_pos
win32_set_cursor_pos
win32_get_clipboard_text
win32_set_clipboard_text
win32_get_system_info
win32_get_time
sleep
beep
```

---

## Operator Precedence

Operators are evaluated in the following order (highest to lowest):

1. **Function calls, member access** `()`, `.`, `[]`
2. **Unary operators** `!`, `-`, `+`
3. **Multiplicative** `*`, `/`, `%`
4. **Additive** `+`, `-`
5. **Relational** `<`, `<=`, `>`, `>=`
6. **Equality** `==`, `!=`
7. **Logical AND** `&&`
8. **Logical OR** `||`
9. **Assignment** `=`, `+=`, `-=`, `*=`, `/=`, `%=`

---

## Type Coercion

HOSC performs automatic type coercion in the following cases:

- **Arithmetic operations**: Integers are promoted to floats when mixed
- **String concatenation**: Numbers and booleans are converted to strings
- **Boolean context**: All types can be used in boolean expressions
  - Numbers: `0` and `0.0` are false, all others are true
  - Strings: Empty string `""` is false, all others are true
  - `null` is always false

---

## Error Handling

### Compile-Time Errors

- Syntax errors (malformed code)
- Type errors (incompatible types)
- Undefined variable errors
- Duplicate declaration errors

### Runtime Errors

- Division by zero
- Array index out of bounds
- Null pointer dereference
- Stack overflow
- Memory allocation failure

---

## Best Practices

1. **Use meaningful variable names**: `userCount` instead of `x`
2. **Declare variables close to use**: Minimize scope
3. **Use constants for magic numbers**: `const MAX_SIZE = 100;`
4. **Comment complex logic**: Explain why, not what
5. **Handle errors gracefully**: Check for null and edge cases
6. **Use functions for reusable code**: DRY principle
7. **Follow consistent naming**: camelCase for variables, PascalCase for types

---

## Version History

- **v1.0** (Current): Initial syntax specification
  - Basic language features
  - Windows API integration
  - HVM bytecode support

---

## References

- [HOSC Language README](readme.md)
- [HOSC Virtual Machine Documentation](hvm_readme.md)
- [HOSC Framework Guide](framework/docs/readme_framework.md)

---

**Document Version:** 1.0  
**Last Updated:** 2024  
**Maintained by:** HOSC Language Team

