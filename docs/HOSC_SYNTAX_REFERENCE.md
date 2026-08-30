# HOSC Language Syntax Reference

**Version 2.0** | Reflects the actual grammar implemented in `compiler/src/lexer.c` and `compiler/src/parser.c`

---

> **Refactor note (2026-07-31):** Version 1.0 of this document described an
> aspirational syntax (`function`, `println`, `debug_print`, compound
> assignment operators, etc.) that does not match what the current compiler
> actually parses. This version was rewritten directly against the lexer and
> parser source so the examples here are guaranteed to compile. See
> [`troubleshooting.md`](troubleshooting.md) for known compiler bugs and
> [`api.md`](api.md) for the diagnostic error codes referenced below.

## Table of Contents

1. [Language Overview](#language-overview)
2. [Lexical Elements](#lexical-elements)
3. [Data Types](#data-types)
4. [Variables and Constants](#variables-and-constants)
5. [Expressions](#expressions)
6. [Statements](#statements)
7. [Functions](#functions)
8. [Control Flow](#control-flow)
9. [Built-in / Framework Functions](#built-in--framework-functions)
10. [Comments](#comments)
11. [Examples](#examples)

---

## Language Overview

A HOSC source file is a `package` declaration followed by top-level
statements and `func` declarations, compiled to HBC bytecode by
`compiler/src/frontend/pipeline.c` and executed by the stack VM in `vm/`.

### Key Features

- A required `package` header and a required `func main()` entry point
- `let`/`var`/`const` declarations with type inference (no type annotations)
- Two print forms: `print` (general expression) and `prints[` `` ` `` raw
  string `` ` `` `]` (backtick-delimited literal text, useful for ASCII art)
- `if`/`while`/`for`/`switch` control flow, no parentheses required around
  conditions
- Dotted-path built-in calls (`audio.play(...)`) used by the optional GUI
  framework layer — `window`/`text` are statement keywords, not dotted
  calls, and using them triggers framework-script detection (see
  [Built-in / Framework Functions](#built-in--framework-functions))

---

## Lexical Elements

### Identifiers

- Must start with a letter (`a-z`, `A-Z`) or underscore (`_`)
- Can contain letters, digits (`0-9`), and underscores
- Case-sensitive
- Cannot be a reserved keyword

```
myVariable
_private
counter123
MAX_SIZE
```

### Keywords

These are the only words the lexer recognizes as keywords (anything else is
an identifier):

```
var          let          const        func
package      import       print        prints
if           else         while        for
return       break        continue     switch
case         default      window       text
true         false
```

There is **no** `function` keyword, no `println`/`debug_print`/`error`/
`warning`/`info` print variants, and no `as` keyword — those do not exist in
the current lexer (`compiler/src/lexer.c`).

### Operators

#### Arithmetic
```
+    Addition
-    Subtraction
*    Multiplication
/    Division
%    Modulo — lexes and parses, but currently fails to compile (see note below)
```

> **Known bug:** `%` is tokenized (`TOKEN_PERCENT`) and parses into a valid
> `AST_BINARY_OP`, but the bytecode emitter's binary-op switch in
> `compiler/src/frontend/pipeline.c` has no case for it and falls through to
> `default: return 0;`. Any expression using `%` fails with diagnostic
> `H900` ("failed to emit bytecode"), even though it type-checks fine. See
> [`troubleshooting.md`](troubleshooting.md).

#### Comparison
```
==   Equal to
!=   Not equal to
<    Less than
<=   Less than or equal to
>    Greater than
>=   Greater than or equal to
```

#### Logical
```
&&   Logical AND
||   Logical OR
!    Logical NOT
```

#### Assignment
```
=    Assignment
```

There are **no** compound assignment operators (`+=`, `-=`, `*=`, `/=`,
`%=`) and no increment/decrement (`++`, `--`) in the lexer. Write them out:
`x = x + 1;` instead of `x += 1;`.

#### Other
```
()   Function call / Grouping
[]   prints[...] delimiter (not general array indexing)
.    Dotted call path (e.g. audio.play)
,    Separator
;    Statement terminator (often optional — see below)
```

### Literals

#### Integer Literals
```
42
1000
```
Parsed with `strtol`; overflow beyond `int` range is a lexer error.

#### Floating-Point Literals
```
3.14
2.71828
1.0
1e10
2.5e-3
```

#### String Literals (double-quoted)
```
"Hello, World!"
"Escaped \"quotes\""
```
Supported escapes: `\n`, `\r`, `\t`, `\"`, `\\`. Double-quoted strings
**cannot** span multiple lines.

#### Raw String Literals (backtick-delimited)
```
`literal text, no escape processing except \` for a literal backtick`
```
Backtick strings **can** span multiple lines and are used exclusively with
`prints[...]` (see below).

#### Boolean Literals
```
true
false
```

There is **no** `null` literal in this grammar.

---

## Data Types

Types are inferred from the value, not declared:

```hosc
let x = 42;           // integer
let y = 3.14;         // float
let name = "HOSC";    // string
let active = true;    // boolean
```

There is no `int`/`float`/`string`/`bool` type-name syntax and no explicit
type annotations — the semantic analyzer (`compiler/src/sema/type_checker.c`)
infers and checks types after parsing.

---

## Variables and Constants

### `let` / `var` Declaration

```
let identifier = expression;
var identifier = expression;
```

`let` and `const` both bind an **immutable** value (the semantic analyzer
tracks them as `SYMBOL_CONST` internally); `var` is the only **mutable**
declaration. This is the reverse of the `let`/`var` convention in
JavaScript — reassigning a `let` in HOSC is diagnostic `H203`, the same
error you get from reassigning a `const`.

```hosc
let count = 0;      // immutable
var price = 99.99;  // mutable — can be reassigned later
```

### `const` Declaration

```
const identifier = expression;
```

```hosc
const PI = 3.14159;
const APP_NAME = "HOSC Application";
```

`const` and `let` are functionally identical for reassignment purposes —
both produce `H203` ("cannot reassign to constant '...'") if you try to
assign to them again.

### Assignment

```hosc
var x = 10;
x = 20;
```

Only `var`-declared identifiers can be reassigned. No `+=`/`-=`/etc. — see
[Operators](#operators).

---

## Expressions

### Arithmetic

```hosc
let sum = 10 + 20;
let product = 5 * 6;
let quotient = 20 / 4;
```

`17 % 5` parses but fails to compile (`H900`) — see the modulo note above.

### Comparison / Logical

```hosc
let isEqual = (x == y);
let both = (x > 0 && y > 0);
let not = !isActive;
```

### String Concatenation

```hosc
let greeting = "Hello" + " " + "World";
```

### Function / Dotted Calls

```hosc
let result = add(5, 3);
audio.play({ file: "song.mp3" });
```

`window(...)` / `text(...)` exist in the grammar too, but calling them
triggers framework-script detection before parsing even runs — see
[Built-in / Framework Functions](#built-in--framework-functions).

Calling an unknown identifier (not a declared `func`, not a known built-in)
produces diagnostic `H205` with a "did you mean" suggestion when a close
match exists.

---

## Statements

### Statement Terminators Are Often Optional

`consume_statement_end` in the parser accepts a `;`, or lets the terminator
be implied when the next token itself starts a new statement (another
keyword, `{`, `}`, end of file, etc.). In practice, always write the `;` —
relying on the implicit form is fragile and not guaranteed by the grammar
long-term.

### Print Statement

```hosc
print "Hello, World!";
print x;
print("Value: " + x);       // parens are optional around the expression
```

### Raw String Print (`prints[...]`)

```hosc
prints[`
literal, multi-line text goes here
`];
```

- Must be `prints[` followed immediately by a backtick raw string, then `]`.
- Anything other than `]` right after the closing backtick — including a
  stray character before it — is diagnostic `H104`
  ("unexpected token '...' after raw string; did you mean ']'?").
- This is the construct that a stray character just before the closing
  backtick (e.g. `` `d]; `` instead of `` `]; ``) will trip.

### Variable Declaration Statement

```hosc
let x = 10;
const name = "HOSC";
```

### Import Statement

```hosc
import "utils.hosc";
```

Only a quoted path is supported at the statement level — see
`compiler/src/module/import_resolver.c` for how imports are resolved before
parsing.

---

## Functions

### Function Declaration

```
func functionName(parameter1, parameter2, ...) {
    // function body
    return value;
}
```

Note the keyword is `func`, not `function`.

```hosc
func add(a, b) {
    return a + b;
}

func greet(name) {
    return "Hello, " + name + "!";
}
```

### Function Call

```hosc
let result = add(5, 3);
let message = greet("HOSC");
```

### Return Statement

```hosc
func square(x) {
    return x * x;
}

func noop() {
    return;   // value is optional
}
```

---

## Control Flow

### If Statement

Conditions do **not** require parentheses (though `(...)` is legal as a
grouped expression):

```hosc
if x > 0 {
    print "Positive";
} else {
    print "Non-positive";
}

if age >= 18 {
    print "Adult";
} else if age >= 13 {
    print "Teenager";
} else {
    print "Child";
}
```

### While Loop

```hosc
var i = 0;
while i < 10 {
    print i;
    i = i + 1;
}
```

### For Loop

C-style, parens **are** required for `for` specifically:

```
for (initialization; condition; increment) {
    // statements
}
```

```hosc
for (var i = 0; i < 10; i = i + 1) {
    print i;
}
```

A bare-condition form (no parens, no init/update) is also accepted:

```hosc
for isRunning {
    // statements
}
```

### Switch Statement

```hosc
switch (value) {
    case 1:
        print "one";
    case 2:
        print "two";
    default:
        print "other";
}
```

There is no fallthrough keyword; each `case`/`default` body runs until the
next `case`/`default`/`}`.

### Break / Continue

```hosc
while true {
    if condition {
        break;
    }
}

for (var i = 0; i < 10; i = i + 1) {
    if i == 5 {
        continue;
    }
    print i;
}
```

(`i % 2 == 0` would be the natural "skip even numbers" condition here, but
`%` currently fails to compile — see the modulo note under
[Operators](#operators).)

---

## Built-in / Framework Functions

The parser has dedicated grammar for `window(...)` and `text(...)` as
statement-level keywords:

```
window "(" String ")" ";"
text "(" Expression "," Expression "," String ")" ";"
```

e.g. `window("Title");` or `text(10, 20, "Label");`. **In practice these
never reach the parser**: `hosc_compile_memory()` scans the raw source for
framework-script markers (`window(`, `text(`, `loop(`, `pump_events(`,
`on_click(`, `on_key(`, `on_mouse_move(`, `win32_message_box(`) *before*
parsing, and if any are present it immediately rejects the file with
diagnostic `H003` ("detected framework GUI script"), regardless of where in
the file they appear or whether the rest of the source is otherwise valid.
Run scripts that need `window`/`text` through
`framework/bin/hosc_framework` instead of `hosc run`.

The one dotted-path built-in that *does* work through the normal pipeline
is `audio`, because `audio` is an ordinary identifier (not a keyword), so
it parses as a regular dotted function call:

```hosc
audio.play("song.mp3");
audio.play({ file: "song.mp3", loop: true });   // config-object form
```

There is no `win32_*` family of built-ins in this grammar (no
`win32_yesno`, `win32_color_dialog`, `sleep`, `beep`, etc.) — those belong
to the separate framework runtime, not the language the compiler parses.

---

## Comments

### Single-Line

```hosc
// This is a single-line comment
let x = 10;  // Comment after code
```

### Multi-Line

```hosc
/*
 * This is a multi-line comment
 * It can span multiple lines
 */
```

Both forms are stripped by the lexer, not passed as tokens.

---

## Examples

### Example 1: Hello World

```hosc
// Hello World in HOSC
package main

func main() {
    print "Hello, World!";
}
```

### Example 2: Raw String / ASCII Art

```hosc
package main

func main() {
    prints[`
 _   _  ___  ____   ____
| | | |/ _ \/ ___| / ___|
| |_| | | | \___ \| |
|  _  | |_| |___) | |___
|_| |_|\___/|____/ \____|
`];
}
```

### Example 3: Variables and Arithmetic

```hosc
package main

func main() {
    let x = 10;
    let y = 20;
    let sum = x + y;

    print "Sum: " + sum;
}
```

### Example 4: Conditional Logic

```hosc
package main

func main() {
    let age = 25;

    if age >= 18 {
        print "You are an adult";
    } else {
        print "You are a minor";
    }
}
```

### Example 5: Loop

```hosc
package main

func main() {
    for (var i = 1; i <= 10; i = i + 1) {
        print i;
    }
}
```

### Example 6: Function

```hosc
package main

func factorial(n) {
    if n <= 1 {
        return 1;
    } else {
        return n * factorial(n - 1);
    }
}

func main() {
    let result = factorial(5);
    print "Factorial of 5: " + result;
}
```

---

## Grammar Summary (EBNF)

```
Program          ::= "package" Identifier Statement*
Statement        ::= VariableDecl | ConstDecl | FunctionDecl | ExpressionStmt |
                     PrintStmt | PrintsStmt | ImportStmt | IfStmt | WhileStmt |
                     ForStmt | SwitchStmt | ReturnStmt | BreakStmt | ContinueStmt
VariableDecl     ::= ("let" | "var") Identifier "=" Expression ";"
ConstDecl        ::= "const" Identifier "=" Expression ";"
FunctionDecl     ::= "func" Identifier "(" ParameterList? ")" Block
ParameterList    ::= Identifier ("," Identifier)*
ExpressionStmt   ::= Expression ";"
PrintStmt        ::= "print" "("? Expression ")"? ";"
PrintsStmt       ::= "prints" "[" RawString "]" ";"
ImportStmt       ::= "import" String ";"
IfStmt           ::= "if" Expression Block ("else" (IfStmt | Block))?
WhileStmt        ::= "while" Expression Block
ForStmt          ::= "for" "(" VariableDecl? ";" Expression? ";" ExpressionStmt? ")" Block  (* init must use "var" to be reassignable in the update clause *)
                    | "for" Expression Block
SwitchStmt       ::= "switch" "(" Expression ")" "{" CaseClause* "}"
CaseClause       ::= ("case" Expression | "default") ":" Statement*
ReturnStmt       ::= "return" Expression? ";"
BreakStmt        ::= "break" ";"
ContinueStmt     ::= "continue" ";"
Block            ::= "{" Statement* "}"
Expression       ::= LogicalOr
LogicalOr        ::= LogicalAnd ("||" LogicalAnd)*
LogicalAnd       ::= Equality ("&&" Equality)*
Equality         ::= Comparison (("==" | "!=") Comparison)*
Comparison       ::= Term (("<" | "<=" | ">" | ">=") Term)*
Term             ::= Factor (("+" | "-") Factor)*
Factor           ::= Unary (("*" | "/" | "%") Unary)*  (* "%" parses but fails codegen — see note below *)
Unary            ::= ("!" | "-") Unary | Primary
Primary          ::= Literal | IdentifierPath (CallArgs)? | "(" Expression ")"
IdentifierPath   ::= Identifier ("." Identifier)*
CallArgs         ::= "(" (Expression ("," Expression)*)? ")"
Literal          ::= Integer | Float | String | RawString | Boolean
Identifier       ::= [a-zA-Z_][a-zA-Z0-9_]*
Integer          ::= [0-9]+
Float            ::= [0-9]+ "." [0-9]+ (("e"|"E") ("+"|"-")? [0-9]+)?
String           ::= '"' ([^"\n\r\\] | EscapeSequence)* '"'
RawString        ::= '`' ([^`\\] | "\`")* '`'
Boolean          ::= "true" | "false"
EscapeSequence   ::= "\\" ("n" | "r" | "t" | '"' | "\\")
```

---

## Operator Precedence

Highest to lowest:

1. **Function calls, dotted paths, grouping** `()`, `.`
2. **Unary operators** `!`, `-`
3. **Multiplicative** `*`, `/`, `%` (`%` parses at this precedence but currently fails to compile — see the modulo note under [Operators](#operators))
4. **Additive** `+`, `-`
5. **Relational** `<`, `<=`, `>`, `>=`
6. **Equality** `==`, `!=`
7. **Logical AND** `&&`
8. **Logical OR** `||`
9. **Assignment** `=`

---

## Diagnostic Error Codes

See [`api.md`](api.md) for the full, current table (`H000`–`H900`). The
codes most relevant while writing syntax:

- `H001` — missing `func main()` entry point
- `H002` — generic/unclassified syntax error
- `H003` — framework GUI script run through the wrong entry point
- `H104` — unknown keyword, or a malformed `prints[...]` closing token
- `H205` — unknown function/identifier call

---

## Best Practices

1. Always write the `func main()` entry point and a leading `package` line.
2. Terminate every statement with `;` even where it's technically optional.
3. Use `prints[...]` only for literal/ASCII-art text; use `print` for any
   expression that needs concatenation or variables.
4. Remember there are no compound assignment operators — write
   `x = x + 1;`, not `x += 1;`.
5. Use `func`, never `function`.

---

## Version History

- **v2.0** (2026-07-31): Rewritten against the actual lexer/parser source.
  Removed aspirational syntax (`function`, `println`, `debug_print`,
  `error`/`warning`/`info` prints, compound assignment operators, `null`,
  `win32_*` built-ins) that the current compiler does not implement, and
  added `prints[...]`, dotted-path calls, `switch`, and paren-optional
  `if`/`while` conditions that v1.0 omitted.
- **v1.0** (2024): Initial syntax specification (aspirational; did not match
  the compiler implementation).

---

## References

- [HOSC Language README](README.md)
- [Troubleshooting](troubleshooting.md)
- [API Reference](api.md)

---

**Document Version:** 2.0
**Last Updated:** 2026-07-31
**Maintained by:** HOSC Language Team
