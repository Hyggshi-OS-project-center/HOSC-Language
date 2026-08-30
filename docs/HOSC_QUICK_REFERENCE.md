# HOSC Language Quick Reference

**Quick syntax reference for HOSC language — matches the actual compiler
grammar (`compiler/src/lexer.c`, `compiler/src/parser.c`), not aspirational
syntax. See [HOSC_SYNTAX_REFERENCE.md](HOSC_SYNTAX_REFERENCE.md) for the
full rewrite notes.**

---

## Program Shape

```hosc
package main

func main() {
    // statements
}
```

A `package` header and a `func main()` entry point are required.

## Variables & Constants

```hosc
let x = 10;                    // Immutable binding (like const)
var y = 5;                     // Mutable — only var can be reassigned
const PI = 3.14159;            // Constant (same as let, functionally)
y = 20;                        // OK: reassigning a var
// x = 20;                     // Error H203: cannot reassign to constant 'x'
```

## Data Types (inferred, no annotations)

```hosc
let int_val = 42;              // Integer
let float_val = 3.14;          // Float
let str_val = "Hello";         // String
let bool_val = true;           // Boolean
```

There is no `null` literal.

## Operators

```hosc
// Arithmetic
+  -  *  /      // '%' also lexes but currently fails to compile (H900) — see full reference

// Comparison
==  !=  <  <=  >  >=

// Logical
&&  ||  !

// Assignment
=
```

**No** compound assignment (`+=`, `-=`, etc.) and **no** `++`/`--` — write
`x = x + 1;` instead.

## Print Statements

```hosc
print "Hello";                 // General expression
print("Value: " + x);          // Parens around expression are optional

prints[`
literal multi-line text
`];                             // Raw backtick string, no interpolation
```

There is no `println`, `debug_print`, `error`, `warning`, or `info` print
variant.

## Control Flow

```hosc
// If statement — no parens required around the condition
if condition {
    // code
} else {
    // code
}

// While loop
while condition {
    // code
}

// For loop — parens required
for (var i = 0; i < 10; i = i + 1) {
    // code
}

// Switch
switch (value) {
    case 1:
        print "one";
    default:
        print "other";
}

// Break and continue
break;
continue;
```

## Functions

```hosc
// Function declaration — keyword is 'func', not 'function'
func add(a, b) {
    return a + b;
}

// Function call
let result = add(5, 3);
```

## Framework Built-ins

```hosc
audio.play("song.mp3");                          // works — 'audio' isn't a keyword
audio.play({ file: "song.mp3", loop: true });     // config-object form
```

`window("Title");` and `text(10, 20, "Label");` exist in the grammar, but
any file containing `window(` or `text(` is intercepted **before parsing**
and rejected with `H003` — run those through `framework/bin/hosc_framework`
instead of `hosc run`. There is no `win32_*` built-in family and no
`sleep`/`beep` statement in this grammar.

## Comments

```hosc
// Single-line comment

/*
 * Multi-line comment
 */
```

## Examples

### Hello World
```hosc
package main

func main() {
    print "Hello, World!";
}
```

### Variables
```hosc
package main

func main() {
    let x = 10;
    let y = 20;
    let sum = x + y;
    print sum;
}
```

### Conditional
```hosc
package main

func main() {
    let x = 5;
    if x > 0 {
        print "Positive";
    } else {
        print "Non-positive";
    }
}
```

### Loop
```hosc
package main

func main() {
    for (var i = 0; i < 10; i = i + 1) {
        print i;
    }
}
```

### Function
```hosc
package main

func square(x) {
    return x * x;
}

func main() {
    let result = square(5);
    print result;
}
```

---

**See [HOSC_SYNTAX_REFERENCE.md](HOSC_SYNTAX_REFERENCE.md) for complete documentation.**
