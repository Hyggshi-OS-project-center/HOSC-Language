# Function List

Generated from current source tree on 2026-04-14.
Heuristic used: top-level function definitions in `.c/.cpp/`, excluding `build/` and generated output folders.

Total functions: 627

## By Module
- `cli`: 12
- `compiler`: 96
- `framework`: 125
- `lsp`: 20
- `runtime`: 273
- `tools`: 42
- `vm`: 37

## cli

### `cli\hosc\src\cli_options.c`
- `hosc_cli_replace_extension`

### `cli\hosc\src\cli_output.c`
- `hosc_cli_print_diagnostics`
- `hosc_cli_print_usage`

### `cli\hosc\src\command_build.c`
- `hosc_cli_command_build`

### `cli\hosc\src\command_check.c`
- `hosc_cli_command_check`

### `cli\hosc\src\command_fmt.c`
- `hosc_cli_command_fmt`

### `cli\hosc\src\command_run.c`
- `hosc_cli_command_run`
- `hosc_cli_command_run_ex`

### `cli\hosc\src\command_test.c`
- `hosc_cli_command_test`

### `cli\hosc\src\command_version.c`
- `hosc_cli_command_version`

### `cli\hosc\src\main.c`
- `main`
- `parse_run`

## compiler

### `compiler\src\arena.c`
- `arena_alloc`
- `arena_create`
- `arena_destroy`
- `arena_reset`
- `arena_strdup`

### `compiler\src\ast_utils.c`
- `ast_get_arena`
- `ast_list_append`
- `ast_release_arena`
- `ast_set_arena`
- `create_ast_node`
- `free_ast`
- `free_list`

### `compiler\src\codegen.c`
- `buf_appendf`
- `buf_ensure`
- `buf_init`
- `build_indent`
- `clear_var_types`
- `emit_function_signature`
- `finalize_codegen`
- `gen_block`
- `gen_function`
- `gen_program`
- `gen_statement`
- `generate_code`
- `init_codegen`
- `remember_var_type`

### `compiler\src\core.c`
- `main`
- `print_usage`

### `compiler\src\diag\diagnostic.c`
- `hosc_diag_bag_add`
- `hosc_diag_bag_free`
- `hosc_diag_bag_init`
- `hosc_diag_has_errors`
- `hosc_diag_strdup`

### `compiler\src\frontend\pipeline.c`
- `hosc_build_bootstrap_bytecode`
- `hosc_compile_file`
- `hosc_compile_memory`
- `hosc_compile_result_free`
- `hosc_dup_range`
- `hosc_extract_print_literal`
- `hosc_looks_like_framework_script`
- `hosc_read_file`
- `hosc_write_bytecode_file`
- `hosc_write_u16`

### `compiler\src\hosc_compiler.c`
- `compile_to_bytecode`
- `file_exists`
- `get_dirname`
- `has_extension`
- `hosc_compile_cli`
- `import_directive_free`
- `is_ident_char`
- `is_ident_start_char`
- `parse_alias_suffix`
- `parse_ident_path_token`
- `parse_import_directive`
- `path_is_absolute`
- `path_join`
- `print_usage`
- `run_c_compiler`
- `sb_append`
- `sb_append_n`
- `sb_free`
- `sb_init`
- `sb_reserve`
- `starts_with_kw`
- `strlist_contains`
- `strlist_free`
- `strlist_pop`
- `strlist_push`
- `strlist_push_unique`
- `write_bytecode`
- `write_c_file`

### `compiler\src\lexer.c`
- `free_partial_tokens`
- `free_tokens`
- `is_ident_part`
- `is_ident_start`
- `lexer_tokenize`
- `make_simple`

### `compiler\src\parser.c`
- `advance_tok`
- `append_text`
- `check`
- `consume_statement_end`
- `count_tokens`
- `current_token`
- `is_assignment_start`
- `is_at_end`
- `match`
- `parser_create`
- `parser_free`
- `parser_parse`
- `parser_parse_expression`
- `parser_parse_from_tokens`
- `parser_parse_program`
- `parser_parse_statement`
- `peek_type`
- `skip_statement`
- `token_starts_statement`

## framework

### `framework\src\hosc_framework.c`
- `add_instruction`
- `collect_frame_events`
- `execute_program`
- `handle_click_events`
- `handle_key_events`
- `handle_mouse_move_events`
- `main`
- `parse_bool`
- `parse_char`
- `parse_identifier`
- `parse_int`
- `parse_loop_begin`
- `parse_loop_block_end`
- `parse_loop_end`
- `parse_loop_statement`
- `parse_message_box_statement`
- `parse_on_click_statement`
- `parse_on_key_statement`
- `parse_on_mouse_move_statement`
- `parse_pump_statement`
- `parse_quoted_string`
- `parse_script_file`
- `parse_statement_end`
- `parse_text_statement`
- `parse_window_statement`
- `print_usage`
- `read_window_statement`
- `skip_spaces`
- `sleep_ms`
- `starts_with`
- `window_statement_complete`

### `framework\src\hosc_modules.c`
- `core_module_cleanup`
- `core_module_get_function`
- `core_module_init`
- `core_module_is_loaded`
- `gui_module_cleanup`
- `gui_module_get_function`
- `gui_module_init`
- `gui_module_is_loaded`
- `io_module_cleanup`
- `io_module_get_function`
- `io_module_init`
- `io_module_is_loaded`
- `math_module_cleanup`
- `math_module_get_function`
- `math_module_init`
- `math_module_is_loaded`
- `string_module_cleanup`
- `string_module_get_function`
- `string_module_init`
- `string_module_is_loaded`
- `win32_module_cleanup`
- `win32_module_get_function`
- `win32_module_init`
- `win32_module_is_loaded`

### `framework\src\hosc_runtime.c`
- `api_get_function`
- `api_list_functions`
- `api_register_function`
- `api_unregister_function`
- `error_clear`
- `error_get_last`
- `error_report`
- `error_set_handler`
- `hosc_allocate`
- `hosc_apply_window_icon`
- `hosc_array_create`
- `hosc_array_destroy`
- `hosc_call_function`
- `hosc_deallocate`
- `hosc_destroy_window_icon`
- `hosc_dictionary_create`
- `hosc_dictionary_destroy`
- `hosc_gui_backend`
- `hosc_gui_backend_name`
- `hosc_gui_clear_event_queue`
- `hosc_gui_create_window`
- `hosc_gui_create_window_ex`
- `hosc_gui_draw_text`
- `hosc_gui_event_queue_is_full`
- `hosc_gui_init`
- `hosc_gui_is_running`
- `hosc_gui_poll_event`
- `hosc_gui_pop_event`
- `hosc_gui_pump_events`
- `hosc_gui_push_event`
- `hosc_gui_shutdown`
- `hosc_load_module`
- `hosc_log`
- `hosc_lparam_x`
- `hosc_lparam_y`
- `hosc_now_ms`
- `hosc_reallocate`
- `hosc_register_window_class`
- `hosc_report_error`
- `hosc_runtime_get_api_registry`
- `hosc_runtime_get_error_handler`
- `hosc_runtime_get_logger`
- `hosc_runtime_get_memory_manager`
- `hosc_runtime_get_module_registry`
- `hosc_runtime_get_state`
- `hosc_runtime_init`
- `hosc_runtime_shutdown`
- `hosc_strdup_local`
- `hosc_string_create`
- `hosc_string_destroy`
- `hosc_unload_module`
- `hosc_window_proc`
- `log_level_to_string`
- `logger_flush`
- `logger_log`
- `logger_set_level`
- `logger_set_output`
- `memory_allocate`
- `memory_deallocate`
- `memory_dump_stats`
- `memory_get_allocated_size`
- `memory_header_size`
- `memory_reallocate`
- `memory_track_alloc`
- `memory_track_free`
- `module_get`
- `module_list_all`
- `module_load`
- `module_registry_add`
- `module_unload`

## lsp

### `lsp\src\completion.ts`
- `allCompletionItems`

### `lsp\src\diagnostics.ts`
- `parseHoscDiagnosticsLine`
- `parseHoscDiagnosticsOutput`
- `toLanguageServerDiagnostics`

### `lsp\src\formatter.ts`
- `formatDocument`

### `lsp\src\hover.ts`
- `hoverForPosition`
- `isIdentChar`
- `wordAtOffset`

### `lsp\src\server.ts`
- `startServer`

### `lsp\src\uri-path.ts`
- `pathsEqual`
- `uriToFsPath`

### `lsp\src\utils.ts`
- `collectWalkSeeds`
- `fileExistsSync`
- `findToolsBinHosc`
- `lookupOnPath`
- `normalizeExecutableCandidate`
- `pathToUriString`
- `resolveHoscExecutable`
- `resolveHoscExecutableWithFileDir`
- `runCommand`

## runtime

### `runtime\src\bundle\exe_stub.c`
- `hosc_bundle_stub_main`

### `runtime\src\embed\embed_api.c`
- `hosc_runtime_create`
- `hosc_runtime_destroy`
- `hosc_runtime_execute`
- `hosc_runtime_register_native`
- `hosc_runtime_run_bytecode`
- `hosc_runtime_run_file`

### `runtime\src\entry\bundle_loader.c`
- `hosc_runtime_load_hbc_file`

### `runtime\src\entry\main_host.c`
- `main`

### `runtime\src\executor.c`
- `clear_vars`
- `eval_expr`
- `eval_expr_value`
- `exec_block`
- `exec_call`
- `exec_call_value`
- `exec_statement`
- `runtime_execute`
- `rv_as_int`
- `rv_copy`
- `rv_float`
- `rv_free`
- `rv_int`
- `rv_string_dup`
- `set_var_value`
- `unset_var`

### `runtime\src\hosc_cpp_api.cpp`
- `align_up`
- `arena_`
- `buffer_`
- `hosc_api_alloc`
- `hosc_api_capacity_bytes`
- `hosc_api_clear`
- `hosc_api_create`
- `hosc_api_destroy`
- `hosc_api_get_int`
- `hosc_api_get_string`
- `hosc_api_set_int`
- `hosc_api_set_string`
- `hosc_api_used_bytes`

### `runtime\src\hvm.c`
- `hvm_create`
- `hvm_destroy`
- `hvm_disassemble`
- `hvm_gc_collect`
- `hvm_gc_live_bytes`
- `hvm_gc_live_objects`
- `hvm_gc_set_enabled`

### `runtime\src\hvm_compiler.c`
- `add_call_patch`
- `add_local_binding`
- `add_loop_patch`
- `clear_local_bindings`
- `clear_loop_stack`
- `compile_block`
- `compile_call_expression`
- `compile_expression`
- `compile_function`
- `compile_statement`
- `emit_float`
- `emit_int`
- `emit_jump`
- `emit_str`
- `ensure_capacity`
- `find_function_index`
- `free_internal`
- `hvm_compile_beep`
- `hvm_compile_expression`
- `hvm_compile_number_literal`
- `hvm_compile_print_statement`
- `hvm_compile_program`
- `hvm_compile_sleep`
- `hvm_compile_statement`
- `hvm_compile_variable_declaration`
- `hvm_compile_win32_error`
- `hvm_compile_win32_info`
- `hvm_compile_win32_message_box`
- `hvm_compile_win32_warning`
- `hvm_compiler_add_error`
- `hvm_compiler_compile_ast`
- `hvm_compiler_create`
- `hvm_compiler_destroy`
- `hvm_compiler_has_errors`
- `hvm_compiler_print_errors`
- `list_count`
- `patch_jump`
- `patch_loop_breaks`
- `pop_loop_context`
- `push_loop_context`
- `register_function`
- `reset_internal_state`
- `set_loop_continue_target`

### `runtime\src\hvm_debug.c`
- `hvm_print_instructions`

### `runtime\src\hvm_error.c`
- `hvm_get_error`
- `hvm_print_stack`
- `hvm_set_error`

### `runtime\src\hvm_execute.c`
- `hvm_color_rgb`
- `hvm_exec_add`
- `hvm_exec_arithmetic`
- `hvm_exec_call`
- `hvm_exec_clear`
- `hvm_exec_command`
- `hvm_exec_compare`
- `hvm_exec_create_window`
- `hvm_exec_delta_time`
- `hvm_exec_draw_button`
- `hvm_exec_draw_button_state`
- `hvm_exec_draw_image`
- `hvm_exec_draw_input`
- `hvm_exec_draw_input_state`
- `hvm_exec_draw_text`
- `hvm_exec_draw_textarea`
- `hvm_exec_file_open_dialog`
- `hvm_exec_file_read`
- `hvm_exec_file_read_line`
- `hvm_exec_file_save_dialog`
- `hvm_exec_file_write`
- `hvm_exec_get_mouse_x`
- `hvm_exec_get_mouse_y`
- `hvm_exec_halt`
- `hvm_exec_input_set`
- `hvm_exec_is_key_down`
- `hvm_exec_is_mouse_down`
- `hvm_exec_is_mouse_hover`
- `hvm_exec_jump`
- `hvm_exec_jump_cond`
- `hvm_exec_layout_column`
- `hvm_exec_layout_grid`
- `hvm_exec_layout_next`
- `hvm_exec_layout_reset`
- `hvm_exec_layout_row`
- `hvm_exec_load_global`
- `hvm_exec_logic`
- `hvm_exec_loop`
- `hvm_exec_menu_event`
- `hvm_exec_menu_setup_notepad`
- `hvm_exec_nop`
- `hvm_exec_not`
- `hvm_exec_pop`
- `hvm_exec_print`
- `hvm_exec_push_bool`
- `hvm_exec_push_float`
- `hvm_exec_push_int`
- `hvm_exec_push_string`
- `hvm_exec_return`
- `hvm_exec_scroll_set_range`
- `hvm_exec_scroll_y`
- `hvm_exec_set_bg_color`
- `hvm_exec_set_color`
- `hvm_exec_set_font_size`
- `hvm_exec_store_global`
- `hvm_exec_textarea_set`
- `hvm_exec_unknown`
- `hvm_exec_was_key_press`
- `hvm_exec_was_mouse_click`
- `hvm_exec_was_mouse_up`
- `hvm_init_dispatch`
- `hvm_run`
- `hvm_value_to_int`
- `hvm_value_to_ll`

### `runtime\src\hvm_gc.c`
- `hvm_gc_collect_internal`
- `hvm_gc_destroy_all`
- `hvm_gc_find_object`
- `hvm_gc_mark_pointer`
- `hvm_gc_mark_roots`
- `hvm_gc_strdup`
- `hvm_gc_sweep`
- `hvm_gc_track_string`

### `runtime\src\hvm_memory.c`
- `find_global_index`
- `hvm_ensure_buffer`
- `hvm_free_value`
- `hvm_is_numeric`
- `hvm_is_truthy`
- `hvm_set_error_msg`
- `hvm_to_double`
- `hvm_value_to_string`
- `hvm_write_text_file`
- `load_global`
- `store_global`

### `runtime\src\hvm_opcode.c`
- `ensure_instruction_capacity`
- `hvm_add_instruction`
- `hvm_add_instruction_address`
- `hvm_add_instruction_float`
- `hvm_add_instruction_string`
- `hvm_clear_instructions`
- `hvm_load_bytecode`
- `opcode_uses_string_operand`

### `runtime\src\hvm_runner.c`
- `bytecode_has_gui_opcodes`
- `env_force_console`
- `free_bytecode`
- `get_self_path`
- `hide_console_window_if_needed`
- `main`
- `opcode_uses_string_operand`
- `str_eq_ci`

### `runtime\src\hvm_stack.c`
- `hvm_peek`
- `hvm_pop`
- `hvm_push_bool`
- `hvm_push_float`
- `hvm_push_int`
- `hvm_push_string`

### `runtime\src\platform\platform_win32.c`
- `hosc_platform_name`

### `runtime\src\runtime_gui.c`
- `gui_apply_click_focus_from_commands`
- `gui_buffer_append_char`
- `gui_buffer_set`
- `gui_clear_commands_internal`
- `gui_create_window_internal`
- `gui_debug_enabled`
- `gui_find_widget_state`
- `gui_free_commands`
- `gui_free_inputs`
- `gui_free_textareas`
- `gui_free_widgets`
- `gui_get_input_state`
- `gui_get_textarea_state`
- `gui_get_widget_state`
- `gui_handle_vscroll`
- `gui_headless_mode`
- `gui_layout_place_widget`
- `gui_menu_setup_notepad_internal`
- `gui_now_ms`
- `gui_point_in_rect`
- `gui_prepare_fallback`
- `gui_pump_events`
- `gui_push_cmd`
- `gui_register_class`
- `gui_register_widget_rect`
- `gui_request_repaint`
- `gui_reset_style_defaults`
- `gui_reset_transient_input_internal`
- `gui_run_loop_until_close_internal`
- `gui_scroll_to`
- `gui_shutdown_internal`
- `gui_take_menu_event`
- `gui_take_mouse_click_in_rect`
- `gui_take_mouse_focus_in_rect`
- `gui_update_scrollbar`
- `hvm_gui_bind_services`
- `hvm_gui_destroy_state`
- `hvm_gui_wndproc`
- `runtime_gui_clear_commands`
- `runtime_gui_create_window`
- `runtime_gui_draw_button`
- `runtime_gui_draw_button_state`
- `runtime_gui_draw_image`
- `runtime_gui_draw_input`
- `runtime_gui_draw_input_state`
- `runtime_gui_draw_text`
- `runtime_gui_draw_textarea`
- `runtime_gui_finish_run`
- `runtime_gui_get_delta_ms`
- `runtime_gui_get_mouse_x`
- `runtime_gui_get_mouse_y`
- `runtime_gui_input_set`
- `runtime_gui_is_key_down`
- `runtime_gui_is_mouse_down`
- `runtime_gui_is_mouse_hover`
- `runtime_gui_layout_column`
- `runtime_gui_layout_grid`
- `runtime_gui_layout_next`
- `runtime_gui_layout_reset`
- `runtime_gui_layout_row`
- `runtime_gui_loop_tick`
- `runtime_gui_menu_event`
- `runtime_gui_menu_setup_notepad`
- `runtime_gui_prepare_run`
- `runtime_gui_scroll_set_range`
- `runtime_gui_scroll_y`
- `runtime_gui_set_bg_color`
- `runtime_gui_set_fg_color`
- `runtime_gui_set_font_size`
- `runtime_gui_shutdown`
- `runtime_gui_textarea_set`
- `runtime_gui_was_key_press`
- `runtime_gui_was_mouse_click`
- `runtime_gui_was_mouse_up`

### `runtime\src\runtime_services.c`
- `hvm_runtime_services_destroy`

## tools

### `tools\hosc_cli.c`
- `ends_with_ci`
- `ensure_capacity`
- `main`
- `print_modern_usage`
- `print_version`
- `read_text_file`
- `run_build_command`
- `run_check_command`
- `run_fmt_command`
- `run_legacy`
- `run_run_command`
- `streq`
- `write_text_file`

### `tools\legacy\codegen_backup.c`
- `codegen_generate`
- `codegen_generate_expression`
- `codegen_generate_program`
- `codegen_generate_statement`

### `tools\legacy\codegen_simple.c`
- `codegen_generate`

### `tools\legacy\hosc_engine.c`
- `hosc_engine_compile`
- `hosc_engine_execute`

### `tools\legacy\hosc_example.c`
- `main`

### `tools\legacy\hosc_lib.c`
- `hosc_cleanup`
- `hosc_compile`
- `hosc_execute`
- `hosc_init`
- `hosc_quick_compile`
- `hosc_quick_execute`

### `tools\legacy\hvm_example.c`
- `main`

### `tools\tests\test_compiler.c`
- `block_stmt`
- `find_function`
- `first_decl`
- `free_program`
- `has_opcode`
- `main`
- `test_codegen_function_program`
- `test_codegen_statement`
- `test_hvm_gui_pipeline`
- `test_lexer`
- `test_parser_binary`
- `test_parser_gui`
- `test_parser_let`
- `test_parser_print`

## vm

### `vm\src\bytecode\loader.c`
- `hvm_bytecode_load_file`
- `hvm_loader_error`

### `vm\src\core\call_frame.c`
- `hvm_pop_frame`
- `hvm_push_frame`

### `vm\src\core\dispatch.c`
- `hvm_read_u16`

### `vm\src\core\interpreter_loop.c`
- `hvm_call_value`
- `hvm_interpret_loop`
- `hvm_peek`
- `hvm_pop`
- `hvm_push`

### `vm\src\core\vm.c`
- `hvm_create`
- `hvm_destroy`
- `hvm_execute`
- `hvm_execute_entry`
- `hvm_last_error`
- `hvm_load_bytecode`
- `hvm_set_error`

### `vm\src\memory\gc_mark_sweep.c`
- `hvm_collect_garbage`
- `hvm_free_all_objects`

### `vm\src\native\native_registry.c`
- `hvm_lookup_native`
- `hvm_native_print`
- `hvm_register_builtin_natives`
- `hvm_register_native`

### `vm\src\object\object.c`
- `hvm_allocate_object`
- `hvm_free_object`

### `vm\src\object\string.c`
- `hvm_hash_string`
- `hvm_native_new`
- `hvm_strdup_len`
- `hvm_string_new`

### `vm\src\object\value.c`
- `hvm_value_bool`
- `hvm_value_equals`
- `hvm_value_float`
- `hvm_value_int`
- `hvm_value_is_truthy`
- `hvm_value_nil`
- `hvm_value_object`
- `hvm_value_print`
