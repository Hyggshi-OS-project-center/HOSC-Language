# VM Architecture

The bootstrap HVM already owns the intended execution boundaries:

- value stack
- call frame stack
- bytecode loader
- native registry
- heap object tracking

Current execution support is intentionally limited to the opcode subset needed for bootstrap bytecode.
