# HOSC Bytecode

Bootstrap `.hbc` layout:

1. `HBCFileHeader`
2. string table
3. constant pool
4. global symbol table
5. function table
6. code blob

Current executable opcode slice:

- `OP_CONSTANT`
- `OP_GET_GLOBAL`
- `OP_CALL`
- `OP_POP`
- `OP_RETURN`
