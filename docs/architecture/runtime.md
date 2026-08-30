# Runtime Architecture

The runtime layer is responsible for:

- loading `.hbc` bytecode
- creating and configuring the VM
- exposing an embedding API
- hosting standalone execution through `hvm_host`

The CLI calls runtime APIs instead of executing VM code directly.
