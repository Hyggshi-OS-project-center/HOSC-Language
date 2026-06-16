#include "hosc_compiler_api.h"
#include "parser.h"

#include <ctype.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct StrBuf {
    char* data;
    size_t len;
    size_t cap;
} StrBuf;

typedef struct PathList {
    char** items;
    size_t count;
    size_t cap;
} PathList;

typedef struct HbcEmitter {
    HBytecode* bc;
    uint8_t* code;
    size_t code_len;
    size_t code_cap;
    ASTNode** functions;
    size_t function_count;
} HbcEmitter;

static char* hosc_read_file(const char* path, size_t* out_length) {
    FILE* file;
    long size;
    size_t read_count;
    char* buffer;

    file = fopen(path, "rb");
    if (!file) return NULL;

    if (fseek(file, 0, SEEK_END) != 0) {
        fclose(file);
        return NULL;
    }
    size = ftell(file);
    if (size < 0) {
        fclose(file);
        return NULL;
    }
    if (fseek(file, 0, SEEK_SET) != 0) {
        fclose(file);
        return NULL;
    }

    buffer = (char*)malloc((size_t)size + 1);
    if (!buffer) {
        fclose(file);
        return NULL;
    }

    read_count = fread(buffer, 1, (size_t)size, file);
    fclose(file);
    if (read_count != (size_t)size) {
        free(buffer);
        return NULL;
    }

    buffer[size] = '\0';
    if (out_length) *out_length = (size_t)size;
    return buffer;
}

static char* hosc_dup_cstr(const char* s) {
    size_t len;
    char* out;
    if (!s) return NULL;
    len = strlen(s);
    out = (char*)malloc(len + 1);
    if (!out) return NULL;
    memcpy(out, s, len + 1);
    return out;
}

static char* hosc_dup_range_len(const char* start, size_t len) {
    char* out = (char*)malloc(len + 1);
    if (!out) return NULL;
    memcpy(out, start, len);
    out[len] = '\0';
    return out;
}

static void sb_free(StrBuf* b) {
    if (!b) return;
    free(b->data);
    b->data = NULL;
    b->len = 0;
    b->cap = 0;
}

static int sb_append_n(StrBuf* b, const char* text, size_t len) {
    char* next;
    size_t new_cap;
    if (!b || !text) return 0;
    if (b->len + len + 1 > b->cap) {
        new_cap = b->cap == 0 ? 256 : b->cap;
        while (new_cap < b->len + len + 1) new_cap *= 2;
        next = (char*)realloc(b->data, new_cap);
        if (!next) return 0;
        b->data = next;
        b->cap = new_cap;
    }
    memcpy(b->data + b->len, text, len);
    b->len += len;
    b->data[b->len] = '\0';
    return 1;
}

static int sb_append(StrBuf* b, const char* text) {
    return sb_append_n(b, text, strlen(text));
}

static char* sb_steal(StrBuf* b) {
    char* out;
    if (!b) return NULL;
    if (!b->data) return hosc_dup_cstr("");
    out = b->data;
    b->data = NULL;
    b->len = 0;
    b->cap = 0;
    return out;
}

static void path_list_free(PathList* list) {
    size_t i;
    if (!list) return;
    for (i = 0; i < list->count; ++i) free(list->items[i]);
    free(list->items);
    list->items = NULL;
    list->count = 0;
    list->cap = 0;
}

static int path_list_contains(PathList* list, const char* path) {
    size_t i;
    if (!list || !path) return 0;
    for (i = 0; i < list->count; ++i) {
        if (strcmp(list->items[i], path) == 0) return 1;
    }
    return 0;
}

static int path_list_push(PathList* list, const char* path) {
    char** next;
    size_t new_cap;
    if (!list || !path) return 0;
    if (list->count == list->cap) {
        new_cap = list->cap == 0 ? 8 : list->cap * 2;
        next = (char**)realloc(list->items, new_cap * sizeof(char*));
        if (!next) return 0;
        list->items = next;
        list->cap = new_cap;
    }
    list->items[list->count] = hosc_dup_cstr(path);
    if (!list->items[list->count]) return 0;
    list->count++;
    return 1;
}

static void path_list_pop(PathList* list) {
    if (!list || list->count == 0) return;
    free(list->items[list->count - 1]);
    list->items[list->count - 1] = NULL;
    list->count--;
}

static int is_path_separator(char c) {
    return c == '/' || c == '\\';
}

static int is_absolute_path(const char* path) {
    if (!path || !path[0]) return 0;
    if (is_path_separator(path[0])) return 1;
    return isalpha((unsigned char)path[0]) && path[1] == ':';
}

static char* dirname_of(const char* path) {
    const char* end;
    if (!path) return hosc_dup_cstr(".");
    end = path + strlen(path);
    while (end > path && !is_path_separator(end[-1])) --end;
    if (end == path) return hosc_dup_cstr(".");
    return hosc_dup_range_len(path, (size_t)(end - path));
}

static char* join_path(const char* base, const char* child) {
    StrBuf b = {0};
    size_t base_len;
    if (!base || !child) return NULL;
    if (is_absolute_path(child)) return hosc_dup_cstr(child);
    base_len = strlen(base);
    if (!sb_append(&b, base)) goto fail;
    if (base_len > 0 && !is_path_separator(base[base_len - 1])) {
        if (!sb_append(&b, "\\")) goto fail;
    }
    if (!sb_append(&b, child)) goto fail;
    return sb_steal(&b);
fail:
    sb_free(&b);
    return NULL;
}

static char* module_to_path(const char* module, int quoted) {
    StrBuf b = {0};
    size_t i;
    if (!module) return NULL;
    for (i = 0; module[i]; ++i) {
        char c = module[i];
        if (!quoted && c == '.') c = '\\';
        if (!sb_append_n(&b, &c, 1)) goto fail;
    }
    if (!quoted && !strstr(module, ".hosc")) {
        if (!sb_append(&b, ".hosc")) goto fail;
    }
    return sb_steal(&b);
fail:
    sb_free(&b);
    return NULL;
}

static const char* skip_spaces(const char* p) {
    while (*p && isspace((unsigned char)*p)) ++p;
    return p;
}

static int starts_with_word(const char* p, const char* word) {
    size_t len = strlen(word);
    if (strncmp(p, word, len) != 0) return 0;
    return !isalnum((unsigned char)p[len]) && p[len] != '_';
}

static char* parse_import_line(const char* line, int* out_quoted) {
    const char* p = skip_spaces(line);
    const char* start;
    if (out_quoted) *out_quoted = 0;
    if (!starts_with_word(p, "import")) return NULL;
    p = skip_spaces(p + 6);
    if (*p == '"') {
        ++p;
        start = p;
        while (*p && *p != '"') ++p;
        if (*p != '"') return NULL;
        if (out_quoted) *out_quoted = 1;
        return hosc_dup_range_len(start, (size_t)(p - start));
    }
    start = p;
    while (*p && (isalnum((unsigned char)*p) || *p == '_' || *p == '.')) ++p;
    if (p == start) return NULL;
    return hosc_dup_range_len(start, (size_t)(p - start));
}

static char* resolve_imports_recursive(const char* path, PathList* active, PathList* loaded, int is_entry) {
    size_t length = 0;
    char* source;
    char* dir = NULL;
    const char* cursor;
    StrBuf out = {0};

    if (path_list_contains(loaded, path)) return hosc_dup_cstr("");
    if (path_list_contains(active, path)) return NULL;
    if (!path_list_push(active, path)) return NULL;

    source = hosc_read_file(path, &length);
    if (!source) goto fail;
    dir = dirname_of(path);
    if (!dir) goto fail;

    cursor = source;
    while (*cursor) {
        const char* line_end = cursor;
        size_t line_len;
        int has_nl;
        char* line;
        char* module;
        int quoted = 0;

        while (*line_end && *line_end != '\n') ++line_end;
        line_len = (size_t)(line_end - cursor);
        has_nl = *line_end == '\n';
        line = hosc_dup_range_len(cursor, line_len);
        if (!line) goto fail;

        module = parse_import_line(line, &quoted);
        if (module) {
            char* module_path = module_to_path(module, quoted);
            char* full_path = module_path ? join_path(dir, module_path) : NULL;
            char* imported = full_path ? resolve_imports_recursive(full_path, active, loaded, 0) : NULL;
            free(module);
            free(module_path);
            free(full_path);
            free(line);
            if (!imported) goto fail;
            if (!sb_append(&out, imported)) {
                free(imported);
                goto fail;
            }
            free(imported);
        } else {
            const char* trimmed = skip_spaces(line);
            if (is_entry || !starts_with_word(trimmed, "package")) {
                if (!sb_append_n(&out, line, line_len)) {
                    free(line);
                    goto fail;
                }
                if (has_nl && !sb_append_n(&out, "\n", 1)) {
                    free(line);
                    goto fail;
                }
            }
            free(line);
        }

        cursor = has_nl ? line_end + 1 : line_end;
    }

    if (!path_list_push(loaded, path)) goto fail;
    path_list_pop(active);
    free(dir);
    free(source);
    return sb_steal(&out);

fail:
    path_list_pop(active);
    free(dir);
    free(source);
    sb_free(&out);
    return NULL;
}

static char* resolve_imports_for_file(const char* path) {
    PathList active = {0};
    PathList loaded = {0};
    char* out = resolve_imports_recursive(path, &active, &loaded, 1);
    path_list_free(&active);
    path_list_free(&loaded);
    return out;
}

static int emit_u8(HbcEmitter* e, uint8_t value) {
    uint8_t* next;
    size_t new_cap;
    if (e->code_len + 1 > e->code_cap) {
        new_cap = e->code_cap == 0 ? 64 : e->code_cap * 2;
        next = (uint8_t*)realloc(e->code, new_cap);
        if (!next) return 0;
        e->code = next;
        e->code_cap = new_cap;
    }
    e->code[e->code_len++] = value;
    return 1;
}

static int emit_op(HbcEmitter* e, HOpcode op) {
    return emit_u8(e, (uint8_t)op);
}

static int emit_u16(HbcEmitter* e, uint16_t value) {
    return emit_u8(e, (uint8_t)(value & 0xffu)) && emit_u8(e, (uint8_t)((value >> 8) & 0xffu));
}

static int add_string(HBytecode* bc, const char* text) {
    HBCString* next;
    size_t index = bc->string_count;
    next = (HBCString*)realloc(bc->strings, (index + 1) * sizeof(HBCString));
    if (!next) return -1;
    bc->strings = next;
    bc->strings[index].length = (uint32_t)strlen(text ? text : "");
    bc->strings[index].bytes = hosc_dup_cstr(text ? text : "");
    if (!bc->strings[index].bytes) return -1;
    bc->string_count++;
    return (int)index;
}

static int add_constant_string(HBytecode* bc, const char* text) {
    HBCConstant* next;
    int string_index = add_string(bc, text);
    size_t index = bc->constant_count;
    if (string_index < 0) return -1;
    next = (HBCConstant*)realloc(bc->constants, (index + 1) * sizeof(HBCConstant));
    if (!next) return -1;
    bc->constants = next;
    bc->constants[index].tag = HBC_CONST_STRING;
    bc->constants[index].as.string_index = (uint32_t)string_index;
    bc->constant_count++;
    return (int)index;
}

static int add_constant_int(HBytecode* bc, int64_t value) {
    HBCConstant* next;
    size_t index = bc->constant_count;
    next = (HBCConstant*)realloc(bc->constants, (index + 1) * sizeof(HBCConstant));
    if (!next) return -1;
    bc->constants = next;
    bc->constants[index].tag = HBC_CONST_INT;
    bc->constants[index].as.int_value = value;
    bc->constant_count++;
    return (int)index;
}

static int add_global(HBytecode* bc, const char* name) {
    HBCGlobalSymbol* next;
    int string_index;
    size_t i;
    for (i = 0; i < bc->global_count; ++i) {
        uint32_t name_index = bc->globals[i].name_string_index;
        if (name_index < bc->string_count && strcmp(bc->strings[name_index].bytes, name) == 0) return (int)i;
    }
    string_index = add_string(bc, name);
    if (string_index < 0) return -1;
    next = (HBCGlobalSymbol*)realloc(bc->globals, (bc->global_count + 1) * sizeof(HBCGlobalSymbol));
    if (!next) return -1;
    bc->globals = next;
    memset(&bc->globals[bc->global_count], 0, sizeof(HBCGlobalSymbol));
    bc->globals[bc->global_count].name_string_index = (uint32_t)string_index;
    bc->globals[bc->global_count].is_mutable = 0;
    bc->global_count++;
    return (int)(bc->global_count - 1);
}

static int emit_indexed(HbcEmitter* e, HOpcode op, int index) {
    if (index < 0 || index > 0xffff) return 0;
    return emit_op(e, op) && emit_u16(e, (uint16_t)index);
}

static int find_local_param(ASTNode* fn, const char* name) {
    size_t i;
    if (!fn || !name) return -1;
    for (i = 0; i < fn->data.function.param_count; ++i) {
        if (strcmp(fn->data.function.params[i], name) == 0) return (int)i + 1;
    }
    return -1;
}

static int compile_expression(HbcEmitter* e, ASTNode* fn, ASTNode* expr);

static int compile_call(HbcEmitter* e, ASTNode* fn, ASTNode* expr) {
    ASTNodeList* arg;
    int global_index;
    int argc = 0;
    global_index = add_global(e->bc, expr->data.call_expr.callee);
    if (!emit_indexed(e, OP_GET_GLOBAL, global_index)) return 0;
    for (arg = expr->data.call_expr.arguments; arg; arg = arg->next) {
        if (!compile_expression(e, fn, arg->node)) return 0;
        argc++;
    }
    return emit_indexed(e, OP_CALL, argc);
}

static int compile_expression(HbcEmitter* e, ASTNode* fn, ASTNode* expr) {
    int index;
    if (!e || !expr) return 0;
    switch (expr->type) {
        case AST_STRING:
            index = add_constant_string(e->bc, expr->data.string_lit.value);
            return emit_indexed(e, OP_CONSTANT, index);
        case AST_NUMBER:
            index = add_constant_int(e->bc, expr->data.number.value);
            return emit_indexed(e, OP_CONSTANT, index);
        case AST_BOOL:
            return emit_op(e, expr->data.boolean.value ? OP_TRUE : OP_FALSE);
        case AST_IDENTIFIER:
            index = find_local_param(fn, expr->data.identifier.name);
            if (index >= 0) return emit_indexed(e, OP_GET_LOCAL, index);
            return emit_op(e, OP_NIL);
        case AST_CALL_EXPR:
            return compile_call(e, fn, expr);
        case AST_UNARY_OP:
            if (!compile_expression(e, fn, expr->data.unary_op.operand)) return 0;
            if (expr->data.unary_op.op == TOKEN_MINUS) return emit_op(e, OP_NEGATE);
            if (expr->data.unary_op.op == TOKEN_BANG) return emit_op(e, OP_NOT);
            return emit_op(e, OP_NIL);
        case AST_BINARY_OP:
            if (!compile_expression(e, fn, expr->data.binary_op.left)) return 0;
            if (!compile_expression(e, fn, expr->data.binary_op.right)) return 0;
            switch (expr->data.binary_op.op) {
                case TOKEN_PLUS: return emit_op(e, OP_ADD);
                case TOKEN_MINUS: return emit_op(e, OP_SUB);
                case TOKEN_STAR: return emit_op(e, OP_MUL);
                case TOKEN_SLASH: return emit_op(e, OP_DIV);
                case TOKEN_EQUAL_EQUAL: return emit_op(e, OP_EQ);
                case TOKEN_BANG_EQUAL: return emit_op(e, OP_NE);
                default: return emit_op(e, OP_NIL);
            }
        default:
            return emit_op(e, OP_NIL);
    }
}

static int compile_statement(HbcEmitter* e, ASTNode* fn, ASTNode* stmt);

static int compile_block(HbcEmitter* e, ASTNode* fn, ASTNode* block) {
    ASTNodeList* cur;
    if (!block || block->type != AST_BLOCK) return 0;
    for (cur = block->data.block.statements; cur; cur = cur->next) {
        if (!compile_statement(e, fn, cur->node)) return 0;
    }
    return 1;
}

static int compile_statement(HbcEmitter* e, ASTNode* fn, ASTNode* stmt) {
    int print_index;
    if (!stmt) return 1;
    switch (stmt->type) {
        case AST_BLOCK:
            return compile_block(e, fn, stmt);
        case AST_PRINT_STATEMENT:
            print_index = add_global(e->bc, "print");
            if (!emit_indexed(e, OP_GET_GLOBAL, print_index)) return 0;
            if (!compile_expression(e, fn, stmt->data.print_statement.expression)) return 0;
            if (!emit_indexed(e, OP_CALL, 1)) return 0;
            return emit_op(e, OP_POP);
        case AST_EXPR_STATEMENT:
            if (!compile_expression(e, fn, stmt->data.expr_stmt.expression)) return 0;
            return emit_op(e, OP_POP);
        case AST_RETURN:
            if (stmt->data.return_stmt.value) {
                if (!compile_expression(e, fn, stmt->data.return_stmt.value)) return 0;
            } else if (!emit_op(e, OP_NIL)) {
                return 0;
            }
            return emit_op(e, OP_RETURN);
        case AST_PACKAGE:
        case AST_IMPORT:
            return 1;
        default:
            return 1;
    }
}

static int collect_functions(ASTNode* ast, ASTNode*** out_functions, size_t* out_count) {
    ASTNodeList* cur;
    ASTNode** items = NULL;
    size_t count = 0;

    if (!ast || ast->type != AST_PROGRAM) return 0;
    for (cur = ast->data.program.declarations; cur; cur = cur->next) {
        if (cur->node && cur->node->type == AST_FUNCTION) {
            ASTNode** next = (ASTNode**)realloc(items, (count + 1) * sizeof(ASTNode*));
            if (!next) {
                free(items);
                return 0;
            }
            items = next;
            items[count++] = cur->node;
        }
    }
    *out_functions = items;
    *out_count = count;
    return 1;
}

static int function_index_by_name(ASTNode** functions, size_t count, const char* name) {
    size_t i;
    if (!name) return -1;
    for (i = 0; i < count; ++i) {
        if (functions[i]->data.function.name && strcmp(functions[i]->data.function.name, name) == 0) return (int)i;
    }
    return -1;
}

static HBytecode* hosc_build_ast_bytecode(ASTNode* ast) {
    HbcEmitter e;
    HBytecode* bc;
    size_t i;
    ASTNode** functions = NULL;
    size_t function_count = 0;
    int main_index;

    if (!collect_functions(ast, &functions, &function_count) || function_count == 0) return NULL;
    main_index = function_index_by_name(functions, function_count, "main");
    if (main_index < 0) {
        free(functions);
        return NULL;
    }

    bc = (HBytecode*)malloc(sizeof(HBytecode));
    if (!bc) {
        free(functions);
        return NULL;
    }
    hbytecode_init(bc);
    bc->function_count = function_count;
    bc->functions = (HBCFunction*)calloc(function_count, sizeof(HBCFunction));
    if (!bc->functions) {
        free(functions);
        free(bc);
        return NULL;
    }
    bc->entry_function_index = (uint32_t)main_index;

    memset(&e, 0, sizeof(e));
    e.bc = bc;
    e.functions = functions;
    e.function_count = function_count;

    for (i = 0; i < function_count; ++i) {
        ASTNode* fn = functions[i];
        int name_index = add_string(bc, fn->data.function.name ? fn->data.function.name : "");
        if (name_index < 0) goto fail;
        bc->functions[i].name_string_index = (uint32_t)name_index;
        bc->functions[i].arity = (uint16_t)fn->data.function.param_count;
        bc->functions[i].local_count = (uint16_t)(fn->data.function.param_count + 1);
        bc->functions[i].max_stack = 64;
        bc->functions[i].flags = 0;
    }

    for (i = 0; i < function_count; ++i) {
        ASTNode* fn = functions[i];
        size_t start = e.code_len;
        bc->functions[i].code_offset = (uint32_t)start;
        if (!compile_block(&e, fn, fn->data.function.body)) goto fail;
        if (e.code_len == start || e.code[e.code_len - 1] != (uint8_t)OP_RETURN) {
            if (!emit_op(&e, OP_NIL) || !emit_op(&e, OP_RETURN)) goto fail;
        }
        bc->functions[i].code_size = (uint32_t)(e.code_len - start);
    }

    bc->code = e.code;
    bc->code_size = e.code_len;
    free(functions);
    return bc;

fail:
    free(functions);
    free(e.code);
    hbytecode_free(bc);
    free(bc);
    return NULL;
}

static int hosc_looks_like_framework_script(const char* source) {
    const char* markers[] = {
        "window(",
        "text(",
        "loop(",
        "pump_events(",
        "on_click(",
        "on_key(",
        "on_mouse_move(",
        "win32_message_box(",
    };
    size_t i;
    if (!source) return 0;
    for (i = 0; i < sizeof(markers) / sizeof(markers[0]); ++i) {
        if (strstr(source, markers[i]) != NULL) return 1;
    }
    return 0;
}

HoscCompileResult hosc_compile_memory(
    const char* display_path,
    const char* source,
    size_t length,
    const HoscCompileOptions* options) {
    HoscCompileResult result;
    HDiagnosticBag* diagnostics;
    ASTNode* ast;
    (void)display_path;
    (void)length;
    (void)options;

    result.success = false;
    result.bytecode = NULL;
    diagnostics = (HDiagnosticBag*)malloc(sizeof(HDiagnosticBag));
    if (!diagnostics) {
        result.diagnostics = NULL;
        return result;
    }
    hosc_diag_bag_init(diagnostics);
    result.diagnostics = diagnostics;

    if (!source || !strstr(source, "func main")) {
        hosc_diag_bag_add(
            diagnostics,
            HOSC_DIAG_ERROR,
            "H001",
            (HoscSourceSpan){1, 1, 1, 1},
            "compiler expected a 'func main()' entry point");
        return result;
    }

    if (hosc_looks_like_framework_script(source)) {
        hosc_diag_bag_add(
            diagnostics,
            HOSC_DIAG_ERROR,
            "H003",
            (HoscSourceSpan){1, 1, 1, 1},
            "detected framework GUI script. Run it with framework/bin/hosc_framework.exe run <file.hosc>");
        return result;
    }

    ast = parser_parse(source);
    if (!ast) {
        hosc_diag_bag_add(
            diagnostics,
            HOSC_DIAG_ERROR,
            "H002",
            (HoscSourceSpan){1, 1, 1, 1},
            "parse failed");
        return result;
    }

    result.bytecode = hosc_build_ast_bytecode(ast);
    ast_release_arena();
    if (!result.bytecode) {
        hosc_diag_bag_add(
            diagnostics,
            HOSC_DIAG_ERROR,
            "H900",
            (HoscSourceSpan){1, 1, 1, 1},
            "failed to emit bytecode");
        return result;
    }

    result.success = true;
    return result;
}

HoscCompileResult hosc_compile_file(const char* path, const HoscCompileOptions* options) {
    HoscCompileResult result;
    char* source;

    source = resolve_imports_for_file(path);
    if (!source) {
        HDiagnosticBag* diagnostics;
        result.success = false;
        result.bytecode = NULL;
        diagnostics = (HDiagnosticBag*)malloc(sizeof(HDiagnosticBag));
        result.diagnostics = diagnostics;
        if (diagnostics) {
            hosc_diag_bag_init(diagnostics);
            hosc_diag_bag_add(
                diagnostics,
                HOSC_DIAG_ERROR,
                "H000",
                (HoscSourceSpan){1, 1, 1, 1},
                "failed to read source file or resolve imports");
        }
        return result;
    }

    result = hosc_compile_memory(path, source, strlen(source), options);
    free(source);
    return result;
}

bool hosc_write_bytecode_file(const char* path, const HBytecode* bytecode) {
    FILE* file;
    HBCFileHeader header;
    size_t i;
    uint32_t count32;

    if (!path || !bytecode) return false;

    file = fopen(path, "wb");
    if (!file) return false;

    memcpy(header.magic, HBC_MAGIC, 4);
    header.version_major = bytecode->version_major;
    header.version_minor = bytecode->version_minor;
    header.flags = 0;
    header.string_count = (uint32_t)bytecode->string_count;
    header.constant_count = (uint32_t)bytecode->constant_count;
    header.global_count = (uint32_t)bytecode->global_count;
    header.function_count = (uint32_t)bytecode->function_count;
    header.code_size = (uint32_t)bytecode->code_size;
    header.entry_function_index = bytecode->entry_function_index;

    if (fwrite(&header, sizeof(header), 1, file) != 1) {
        fclose(file);
        return false;
    }

    for (i = 0; i < bytecode->string_count; ++i) {
        count32 = bytecode->strings[i].length;
        if (fwrite(&count32, sizeof(count32), 1, file) != 1) {
            fclose(file);
            return false;
        }
        if (count32 > 0 && fwrite(bytecode->strings[i].bytes, 1, count32, file) != count32) {
            fclose(file);
            return false;
        }
    }

    if (bytecode->constant_count > 0 &&
        fwrite(bytecode->constants, sizeof(HBCConstant), bytecode->constant_count, file) != bytecode->constant_count) {
        fclose(file);
        return false;
    }
    if (bytecode->global_count > 0 &&
        fwrite(bytecode->globals, sizeof(HBCGlobalSymbol), bytecode->global_count, file) != bytecode->global_count) {
        fclose(file);
        return false;
    }
    if (bytecode->function_count > 0 &&
        fwrite(bytecode->functions, sizeof(HBCFunction), bytecode->function_count, file) != bytecode->function_count) {
        fclose(file);
        return false;
    }
    if (bytecode->code_size > 0 &&
        fwrite(bytecode->code, 1, bytecode->code_size, file) != bytecode->code_size) {
        fclose(file);
        return false;
    }

    fclose(file);
    return true;
}

void hosc_compile_result_free(HoscCompileResult* result) {
    if (!result) return;
    if (result->bytecode) {
        hbytecode_free(result->bytecode);
        free(result->bytecode);
    }
    if (result->diagnostics) {
        hosc_diag_bag_free(result->diagnostics);
        free(result->diagnostics);
    }
    result->bytecode = NULL;
    result->diagnostics = NULL;
    result->success = false;
}
