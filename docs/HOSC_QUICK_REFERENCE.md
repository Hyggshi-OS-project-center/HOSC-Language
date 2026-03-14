# HOSC Language Quick Reference

**Quick syntax reference for HOSC language**

---

## Variables & Constants

```hosc
let x = 10;                    // Variable
const PI = 3.14159;            // Constant
x = 20;                        // Reassignment
```

## Data Types

```hosc
let int_val = 42;              // Integer
let float_val = 3.14;          // Float
let str_val = "Hello";         // String
let bool_val = true;           // Boolean
let null_val = null;           // Null
```

## Operators

```hosc
// Arithmetic
+  -  *  /  %

// Comparison
==  !=  <  <=  >  >=

// Logical
&&  ||  !

// Assignment
=  +=  -=  *=  /=  %=
```

## Print Statements

```hosc
print "Hello";                 // Print without newline
println "World";               // Print with newline
debug_print "Debug info";     // Debug print
error "Error message";         // Error print
warning "Warning message";     // Warning print
info "Info message";          // Info print
```

## Control Flow

```hosc
// If statement
if (condition) {
    // code
} else {
    // code
}

// While loop
while (condition) {
    // code
}

// For loop
for (let i = 0; i < 10; i = i + 1) {
    // code
}

// Break and continue
break;
continue;
```

## Functions

```hosc
// Function declaration
function add(a, b) {
    return a + b;
}

// Function call
let result = add(5, 3);
```

## Windows API

```hosc
win32_message_box "Message";
win32_error "Error";
win32_info "Info";
win32_warning "Warning";
win32_yesno "Question";
win32_create_window "Title", "Message";
sleep 1000;                    // Milliseconds
beep 1000;                     // Frequency
```

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
print "Hello, World!";
```

### Variables
```hosc
let x = 10;
let y = 20;
let sum = x + y;
print sum;
```

### Conditional
```hosc
if (x > 0) {
    print "Positive";
} else {
    print "Non-positive";
}
```

### Loop
```hosc
for (let i = 0; i < 10; i = i + 1) {
    print i;
}
```

### Function
```hosc
function square(x) {
    return x * x;
}

let result = square(5);
print result;
```

---

**See [hosc_syntax_reference.md](hosc_syntax_reference.md) for complete documentation.**

