add_library(hvm STATIC
  src/object/value.c
  src/object/object.c
  src/object/string.c
  src/memory/gc_mark_sweep.c
  src/native/native_registry.c
  src/bytecode/loader.c
  src/core/call_frame.c
  src/core/dispatch.c
  src/core/interpreter_loop.c
  src/core/vm.c
)

target_include_directories(hvm
  PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_SOURCE_DIR}/compiler/include
)

if(WIN32)
  target_link_libraries(hvm PUBLIC winmm)
endif()
