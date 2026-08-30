# HOSC Syntax

Level A bootstrap syntax currently recognizes a narrow executable slice:

```hosc
package main

func main() {
    print("Hello, HOSC");
}
```

The full lexer/parser pipeline is scaffolded in `compiler/src/*` and will replace the bootstrap parser path next.
