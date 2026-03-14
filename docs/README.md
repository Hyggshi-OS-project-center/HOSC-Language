# HOSC Language

HOSC is a new programming language designed to provide a simple and concise syntax while maintaining the power and flexibility of C. This project aims to create a robust compiler and runtime environment for HOSC, enabling developers to write efficient and readable code.

## Features

- **Concise Syntax**: HOSC offers an easy-to-remember syntax that reduces boilerplate code.
- **Compiler**: The HOSC compiler translates HOSC source code into executable code.
- **Runtime**: A lightweight runtime environment to execute HOSC programs.

## Project Structure

- `src/compiler/`: Contains the compiler components including the lexer, parser, and code generator.
- `src/runtime/`: Contains the core runtime functionalities.
- `src/include/`: Header files for the compiler components.
- `tests/`: Unit tests for the compiler.
- `examples/`: Example HOSC programs.
- `Makefile`: Build instructions for the project.

## Installation

To build the HOSC language, clone the repository and run the following command in the project directory:

```bash
make
```

## Usage

After building the project, you can compile HOSC programs using the HOSC compiler. For example, to compile the `hello.hosc` program, use:

```bash
./hosc-compiler examples/hello.hosc
```

## Contributing

Contributions are welcome! Please open an issue or submit a pull request for any enhancements or bug fixes.

## License

This project is licensed under the MIT license. See `license.txt` for more details.
