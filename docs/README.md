# HOSC Documentation Index

This directory contains source-derived documentation for the current HOSC
repository. Prefer these files when older references disagree with the code.

## Core Project Docs

| Document | Purpose |
| --- | --- |
| [setup.md](setup.md) | Local development prerequisites and environment setup. |
| [build.md](build.md) | Bootstrap, CMake, framework, editor, and CI build commands. |
| [api.md](api.md) | Public C APIs, CLI commands, bytecode structures, and editor surfaces. |
| [architecture.md](architecture.md) | Module graph, runtime workflow, dependency graph, and project boundaries. |
| [troubleshooting.md](troubleshooting.md) | Known errors, stale paths, build failures, and cleanup suggestions. |
| [SOURCE_DERIVED_SNAPSHOT.md](SOURCE_DERIVED_SNAPSHOT.md) | Historical source snapshot used during the documentation refresh. |

## Language and Architecture Notes

| Path | Notes |
| --- | --- |
| [language/syntax.md](language/syntax.md) | Syntax notes for HOSC source. Validate against compiler behavior before treating as complete. |
| [language/semantics.md](language/semantics.md) | Semantic notes and language design direction. |
| [language/bytecode.md](language/bytecode.md) | HBC bytecode notes. |
| [architecture/compiler.md](architecture/compiler.md) | Compiler component notes. |
| [architecture/runtime.md](architecture/runtime.md) | Runtime component notes. |
| [architecture/vm.md](architecture/vm.md) | VM component notes. |

## Legacy References

The following files may still be useful for design context, but some content is
ahead of the bootstrap implementation:

- [HOSC_QUICK_REFERENCE.md](HOSC_QUICK_REFERENCE.md)
- [HOSC_SYNTAX_REFERENCE.md](HOSC_SYNTAX_REFERENCE.md)
- [FUNCTION_LIST.md](FUNCTION_LIST.md)
- [document.md](document.md)
- [framework_maturity.md](framework_maturity.md)
- [CPP_MEMCHECK.md](CPP_MEMCHECK.md)

## Trust Order

When documents disagree, use this order:

1. Current source files and headers
2. `CMakeLists.txt` files
3. `tools/build.ps1` and `framework/build.ps1`
4. `README.md` and the docs listed in "Core Project Docs"
5. Older language/design references
