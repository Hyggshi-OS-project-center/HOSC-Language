add_library(hosc_compiler STATIC
  src/diag/diagnostic.c
  src/arena.c
  src/ast_utils.c
  src/lexer.c
  src/parser.c
  src/lexer/lexer.c
  src/parser/parser.c
  src/ast/ast_nodes.c
  src/sema/symbol_table.c
  src/sema/type_checker.c
  src/ir/lowered_ir.c
  src/codegen/constant_pool.c
  src/codegen/bytecode_emitter.c
  src/module/import_resolver.c
  src/frontend/compile_session.c
  src/frontend/pipeline.c
)

target_include_directories(hosc_compiler
  PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_SOURCE_DIR}/vm/include
)
