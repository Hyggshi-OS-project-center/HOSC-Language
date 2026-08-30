# Compiler Architecture

The bootstrap compiler exposes the final public API shape now:

- `hosc_compile_file`
- `hosc_compile_memory`
- `hosc_write_bytecode_file`
- `hosc_compile_result_free`

`compiler/src/frontend/pipeline.c` currently implements a narrow bootstrap path while the structured lexer/parser/sema/IR files are staged for full Level A implementation.
