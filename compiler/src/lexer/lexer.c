#include "hosc_compiler_api.h"

/*
 * Level A bootstrap note:
 * The real lexer will live here. The pipeline currently uses a narrow-path
 * bootstrap compiler implemented in frontend/pipeline.c so the public API and
 * build graph can stabilize before the full frontend lands.
 */
