# Fixed Project Structure

This file is retained for historical reference. The current, authoritative layout
is documented in `docs/project_structure.md`.

Key updates captured by the refactor:

- Framework uses the compiler + HVM pipeline (no custom parser/executor).
- GUI is implemented via runtime services (`services/`).
- Legacy framework runtime code moved to `framework/legacy`.
