#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "hvm_compiler.h"

typedef struct {
    char *name;
    ASTNode *ast;
    size_t entry;
    int entry_set;
} FunctionEntry;

typedef struct {
    size_t instruction_index;
    size_t function_index;
} CallPatch;

typedef struct {
    char *name;
    char *scoped_name;
} LocalBinding;

typedef struct {
    int *break_patches;
    size_t break_count;
    size_t break_cap;

    int *continue_patches;
    size_t continue_count;
    size_t continue_cap;

    size_t continue_target;
    int has_continue_target;
} LoopContext;

typedef struct {
    FunctionEntry *functions;
    size_t function_count;
    size_t function_cap;

    CallPatch *call_patches;
    size_t call_patch_count;
    size_t call_patch_cap;

    LocalBinding *locals;
    size_t local_count;
    size_t local_cap;

    LoopContext *loop_stack;
    size_t loop_depth;
    size_t loop_cap;

    const char *current_function;
} CompilerInternal;

static int compile_expression(HVM_Compiler* compiler, ASTNode* ast);
static int compile_statement(HVM_Compiler* compiler, ASTNode* ast);
static int compile_block(HVM_Compiler* compiler, ASTNode* ast);

static CompilerInternal *ci(HVM_Compiler *compiler) {
    return compiler ? (CompilerInternal *)compiler->internal : NULL;
}

static int ensure_capacity(void **buffer, size_t *cap, size_t elem_size, size_t needed) {
    size_t new_cap;
    void *new_buf;
    if (needed <= *cap) return 1;
    new_cap = (*cap == 0) ? 8 : *cap;
    while (new_cap < needed) new_cap *= 2;
    new_buf = realloc(*buffer, elem_size * new_cap);
    if (!new_buf) return 0;
    *buffer = new_buf;
    *cap = new_cap;
    return 1;
}

static char *dup_heap(const char *s) {
    size_t len;
    char *out;
    if (!s) return NULL;
    len = strlen(s);
    out = (char *)malloc(len + 1);
    if (!out) return NULL;
    memcpy(out, s, len + 1);
    return out;
}

static char *make_scoped_name(const char *function_name, const char *name) {
    int needed;
    char *out;
    if (!function_name || !name) return NULL;
    needed = snprintf(NULL, 0, "__%s$%s", function_name, name);
    if (needed < 0) return NULL;
    out = (char *)malloc((size_t)needed + 1);
    if (!out) return NULL;
    snprintf(out, (size_t)needed + 1, "__%s$%s", function_name, name);
    return out;
}

static int emit_int(HVM_Compiler* c, HVM_Opcode op, int64_t v) { return hvm_add_instruction(c->vm, op, v); }
static int emit_float(HVM_Compiler* c, HVM_Opcode op, double v) { return hvm_add_instruction_float(c->vm, op, v); }
static int emit_str(HVM_Compiler* c, HVM_Opcode op, const char* s) { return hvm_add_instruction_string(c->vm, op, s); }
static int emit_jump(HVM_Compiler* c, HVM_Opcode op) {
    size_t idx = c->vm->instruction_count;
    if (!emit_int(c, op, 0)) return -1;
    return (int)idx;
}
static void patch_jump(HVM_Compiler* c, int index, size_t target) {
    if (index >= 0) c->vm->instructions[index].operand.int_operand = (int64_t)target;
}

static int list_count(ASTNodeList *list) {
    int n = 0;
    while (list) {
        n++;
        list = list->next;
    }
    return n;
}

void hvm_compiler_add_error(HVM_Compiler* compiler, const char* message) {
    if (!compiler || !message) return;
    compiler->error_count++;
    compiler->error_messages = (char **)realloc(compiler->error_messages, sizeof(char*) * (size_t)compiler->error_count);
    if (compiler->error_messages) compiler->error_messages[compiler->error_count - 1] = dup_heap(message);
}

HVM_Compiler* hvm_compiler_create(HVM_VM* vm) {
    HVM_Compiler* c;
    CompilerInternal *internal;
    if (!vm) return NULL;

    c = (HVM_Compiler*)calloc(1, sizeof(HVM_Compiler));
    if (!c) return NULL;

    internal = (CompilerInternal *)calloc(1, sizeof(CompilerInternal));
    if (!internal) {
        free(c);
        return NULL;
    }

    c->vm = vm;
    c->internal = internal;
    return c;
}

static void free_internal(CompilerInternal *internal) {
    size_t i;
    if (!internal) return;

    for (i = 0; i < internal->function_count; i++) {
        free(internal->functions[i].name);
    }
    free(internal->functions);

    free(internal->call_patches);

    for (i = 0; i < internal->local_count; i++) {
        free(internal->locals[i].name);
        free(internal->locals[i].scoped_name);
    }
    free(internal->locals);

    for (i = 0; i < internal->loop_depth; i++) {
        free(internal->loop_stack[i].break_patches);
        free(internal->loop_stack[i].continue_patches);
    }
    free(internal->loop_stack);

    free(internal);
}

void hvm_compiler_destroy(HVM_Compiler* compiler) {
    int i;
    if (!compiler) return;

    if (compiler->error_messages) {
        for (i = 0; i < compiler->error_count; i++) free(compiler->error_messages[i]);
        free(compiler->error_messages);
    }

    free_internal((CompilerInternal *)compiler->internal);
    free(compiler);
}

static void clear_local_bindings(HVM_Compiler *compiler) {
    CompilerInternal *internal = ci(compiler);
    size_t i;
    if (!internal) return;
    for (i = 0; i < internal->local_count; i++) {
        free(internal->locals[i].name);
        free(internal->locals[i].scoped_name);
    }
    internal->local_count = 0;
}

static int add_local_binding(HVM_Compiler *compiler, const char *name, const char **out_scoped) {
    CompilerInternal *internal = ci(compiler);
    size_t i;

    if (!internal || !name) return 0;

    for (i = 0; i < internal->local_count; i++) {
        if (strcmp(internal->locals[i].name, name) == 0) {
            if (out_scoped) *out_scoped = internal->locals[i].scoped_name;
            return 1;
        }
    }

    if (!internal->current_function) {
        if (out_scoped) *out_scoped = name;
        return 1;
    }

    if (!ensure_capacity((void **)&internal->locals, &internal->local_cap, sizeof(LocalBinding), internal->local_count + 1)) {
        return 0;
    }

    internal->locals[internal->local_count].name = dup_heap(name);
    internal->locals[internal->local_count].scoped_name = make_scoped_name(internal->current_function, name);
    if (!internal->locals[internal->local_count].name || !internal->locals[internal->local_count].scoped_name) {
        free(internal->locals[internal->local_count].name);
        free(internal->locals[internal->local_count].scoped_name);
        return 0;
    }

    if (out_scoped) *out_scoped = internal->locals[internal->local_count].scoped_name;
    internal->local_count++;
    return 1;
}

static const char *find_local_binding(HVM_Compiler *compiler, const char *name) {
    CompilerInternal *internal = ci(compiler);
    size_t i;
    if (!internal || !name) return NULL;
    for (i = 0; i < internal->local_count; i++) {
        if (strcmp(internal->locals[i].name, name) == 0) return internal->locals[i].scoped_name;
    }
    return NULL;
}

static void clear_loop_stack(CompilerInternal *internal) {
    size_t i;
    if (!internal) return;
    for (i = 0; i < internal->loop_depth; i++) {
        free(internal->loop_stack[i].break_patches);
        free(internal->loop_stack[i].continue_patches);
        internal->loop_stack[i].break_patches = NULL;
        internal->loop_stack[i].continue_patches = NULL;
        internal->loop_stack[i].break_count = 0;
        internal->loop_stack[i].continue_count = 0;
    }
    internal->loop_depth = 0;
}

static LoopContext *current_loop_context(HVM_Compiler *compiler) {
    CompilerInternal *internal = ci(compiler);
    if (!internal || internal->loop_depth == 0) return NULL;
    return &internal->loop_stack[internal->loop_depth - 1];
}

static int push_loop_context(HVM_Compiler *compiler, size_t continue_target, int has_continue_target) {
    CompilerInternal *internal = ci(compiler);
    LoopContext *ctx;
    if (!internal) return 0;

    if (!ensure_capacity((void **)&internal->loop_stack, &internal->loop_cap, sizeof(LoopContext), internal->loop_depth + 1)) {
        return 0;
    }

    ctx = &internal->loop_stack[internal->loop_depth];
    memset(ctx, 0, sizeof(*ctx));
    ctx->continue_target = continue_target;
    ctx->has_continue_target = has_continue_target;
    internal->loop_depth++;
    return 1;
}

static void pop_loop_context(HVM_Compiler *compiler) {
    CompilerInternal *internal = ci(compiler);
    LoopContext *ctx;
    if (!internal || internal->loop_depth == 0) return;
    internal->loop_depth--;
    ctx = &internal->loop_stack[internal->loop_depth];
    free(ctx->break_patches);
    free(ctx->continue_patches);
    memset(ctx, 0, sizeof(*ctx));
}

static int add_loop_patch(HVM_Compiler *compiler, int jump_index, int is_continue) {
    LoopContext *ctx = current_loop_context(compiler);
    int *buf;

    if (!ctx) return 0;
    if (is_continue && ctx->has_continue_target) {
        patch_jump(compiler, jump_index, ctx->continue_target);
        return 1;
    }

    if (is_continue) {
        if (!ensure_capacity((void **)&ctx->continue_patches, &ctx->continue_cap, sizeof(int), ctx->continue_count + 1)) {
            return 0;
        }
        ctx->continue_patches[ctx->continue_count++] = jump_index;
        return 1;
    }

    buf = ctx->break_patches;
    if (!ensure_capacity((void **)&ctx->break_patches, &ctx->break_cap, sizeof(int), ctx->break_count + 1)) {
        ctx->break_patches = buf;
        return 0;
    }
    ctx->break_patches[ctx->break_count++] = jump_index;
    return 1;
}

static void set_loop_continue_target(HVM_Compiler *compiler, size_t target) {
    LoopContext *ctx = current_loop_context(compiler);
    size_t i;
    if (!ctx) return;
    ctx->continue_target = target;
    ctx->has_continue_target = 1;
    for (i = 0; i < ctx->continue_count; i++) {
        patch_jump(compiler, ctx->continue_patches[i], target);
    }
    ctx->continue_count = 0;
}

static void patch_loop_breaks(HVM_Compiler *compiler, size_t target) {
    LoopContext *ctx = current_loop_context(compiler);
    size_t i;
    if (!ctx) return;
    for (i = 0; i < ctx->break_count; i++) {
        patch_jump(compiler, ctx->break_patches[i], target);
    }
    ctx->break_count = 0;
}


static int find_function_index(HVM_Compiler *compiler, const char *name) {
    CompilerInternal *internal = ci(compiler);
    size_t i;
    if (!internal || !name) return -1;
    for (i = 0; i < internal->function_count; i++) {
        if (strcmp(internal->functions[i].name, name) == 0) return (int)i;
    }
    return -1;
}

static int add_call_patch(HVM_Compiler *compiler, size_t instruction_index, size_t function_index) {
    CompilerInternal *internal = ci(compiler);
    if (!internal) return 0;
    if (!ensure_capacity((void **)&internal->call_patches, &internal->call_patch_cap, sizeof(CallPatch), internal->call_patch_count + 1)) {
        return 0;
    }
    internal->call_patches[internal->call_patch_count].instruction_index = instruction_index;
    internal->call_patches[internal->call_patch_count].function_index = function_index;
    internal->call_patch_count++;
    return 1;
}

static int register_function(HVM_Compiler *compiler, ASTNode *fn) {
    CompilerInternal *internal = ci(compiler);
    if (!internal || !fn || fn->type != AST_FUNCTION || !fn->data.function.name) return 0;

    if (find_function_index(compiler, fn->data.function.name) >= 0) {
        hvm_compiler_add_error(compiler, "Duplicate function name");
        return 0;
    }

    if (!ensure_capacity((void **)&internal->functions, &internal->function_cap, sizeof(FunctionEntry), internal->function_count + 1)) {
        return 0;
    }

    internal->functions[internal->function_count].name = dup_heap(fn->data.function.name);
    internal->functions[internal->function_count].ast = fn;
    internal->functions[internal->function_count].entry = 0;
    internal->functions[internal->function_count].entry_set = 0;
    if (!internal->functions[internal->function_count].name) {
        return 0;
    }

    internal->function_count++;
    return 1;
}

static void reset_internal_state(HVM_Compiler *compiler) {
    CompilerInternal *internal = ci(compiler);
    size_t i;
    if (!internal) return;

    for (i = 0; i < internal->function_count; i++) {
        free(internal->functions[i].name);
    }
    internal->function_count = 0;

    internal->call_patch_count = 0;
    clear_local_bindings(compiler);
    clear_loop_stack(internal);
    internal->current_function = NULL;
}

static int compile_call_expression(HVM_Compiler *compiler, ASTNode *call_expr) {
    CompilerInternal *internal = ci(compiler);
    ASTNodeList *arg_node;
    const char *name;
    int fn_index;

    if (!internal || !call_expr || call_expr->type != AST_CALL_EXPR) return 0;

    name = call_expr->data.call_expr.callee;
    arg_node = call_expr->data.call_expr.arguments;

    if (strcmp(name, "loop") == 0) {
        for (; arg_node; arg_node = arg_node->next) {
            if (!compile_expression(compiler, arg_node->node)) return 0;
            if (!emit_int(compiler, HVM_POP, 0)) return 0;
        }
        return emit_int(compiler, HVM_LOOP, 0);
    }

    if (strcmp(name, "warn") == 0 || strcmp(name, "warning") == 0 || strcmp(name, "win32.warning") == 0 || strcmp(name, "win32_warning") == 0) {
        ASTNodeList *a = call_expr->data.call_expr.arguments;

        if (!emit_str(compiler, HVM_PUSH_STRING, "[WARNING] ")) return 0;
        if (!emit_int(compiler, HVM_PRINT, 0)) return 0;

        if (a) {
            if (!compile_expression(compiler, a->node)) return 0;
            a = a->next;
        } else {
            if (!emit_str(compiler, HVM_PUSH_STRING, "")) return 0;
        }

        if (!emit_int(compiler, HVM_PRINTLN, 0)) return 0;

        for (; a; a = a->next) {
            if (!compile_expression(compiler, a->node)) return 0;
            if (!emit_int(compiler, HVM_POP, 0)) return 0;
        }

        return emit_int(compiler, HVM_PUSH_INT, 0);
    }
    if (strcmp(name, "clear") == 0) {
        int i; ASTNodeList *a = call_expr->data.call_expr.arguments;
        for (i = 0; i < 3; i++) {
            if (a) { if (!compile_expression(compiler, a->node)) return 0; a = a->next; }
            else { if (!emit_int(compiler, HVM_PUSH_INT, 0)) return 0; }
        }
        for (; a; a = a->next) { if (!compile_expression(compiler, a->node)) return 0; if (!emit_int(compiler, HVM_POP, 0)) return 0; }
        return emit_int(compiler, HVM_CLEAR, 0);
    }

    if (strcmp(name, "bgcolor") == 0 || strcmp(name, "background") == 0) {
        int i; ASTNodeList *a = call_expr->data.call_expr.arguments;
        for (i = 0; i < 3; i++) {
            if (a) { if (!compile_expression(compiler, a->node)) return 0; a = a->next; }
            else { if (!emit_int(compiler, HVM_PUSH_INT, 0)) return 0; }
        }
        for (; a; a = a->next) { if (!compile_expression(compiler, a->node)) return 0; if (!emit_int(compiler, HVM_POP, 0)) return 0; }
        return emit_int(compiler, HVM_SET_BG_COLOR, 0);
    }

    if (strcmp(name, "color") == 0 || strcmp(name, "fgcolor") == 0) {
        int i; ASTNodeList *a = call_expr->data.call_expr.arguments;
        for (i = 0; i < 3; i++) {
            if (a) { if (!compile_expression(compiler, a->node)) return 0; a = a->next; }
            else { if (!emit_int(compiler, HVM_PUSH_INT, 0)) return 0; }
        }
        for (; a; a = a->next) { if (!compile_expression(compiler, a->node)) return 0; if (!emit_int(compiler, HVM_POP, 0)) return 0; }
        return emit_int(compiler, HVM_SET_COLOR, 0);
    }

    if (strcmp(name, "font") == 0 || strcmp(name, "font_size") == 0) {
        ASTNodeList *a = call_expr->data.call_expr.arguments;
        if (a) { if (!compile_expression(compiler, a->node)) return 0; a = a->next; }
        else { if (!emit_int(compiler, HVM_PUSH_INT, 16)) return 0; }
        for (; a; a = a->next) { if (!compile_expression(compiler, a->node)) return 0; if (!emit_int(compiler, HVM_POP, 0)) return 0; }
        return emit_int(compiler, HVM_SET_FONT_SIZE, 0);
    }

    if (strcmp(name, "text") == 0 || strcmp(name, "label") == 0 || strcmp(name, "draw_text") == 0) {
        ASTNodeList *a = call_expr->data.call_expr.arguments; int i;
        for (i = 0; i < 2; i++) {
            if (a) { if (!compile_expression(compiler, a->node)) return 0; a = a->next; }
            else { if (!emit_int(compiler, HVM_PUSH_INT, 0)) return 0; }
        }
        if (a) { if (!compile_expression(compiler, a->node)) return 0; a = a->next; }
        else { if (!emit_str(compiler, HVM_PUSH_STRING, "")) return 0; }
        for (; a; a = a->next) { if (!compile_expression(compiler, a->node)) return 0; if (!emit_int(compiler, HVM_POP, 0)) return 0; }
        return emit_int(compiler, HVM_DRAW_TEXT, 0);
    }

    if (strcmp(name, "button") == 0) {
        ASTNodeList *a = call_expr->data.call_expr.arguments;
        int argc = list_count(a);
        int i;

        if (argc <= 2) {
            if (a) { if (!compile_expression(compiler, a->node)) return 0; a = a->next; }
            else { if (!emit_str(compiler, HVM_PUSH_STRING, "button")) return 0; }

            if (a) { if (!compile_expression(compiler, a->node)) return 0; a = a->next; }
            else { if (!emit_str(compiler, HVM_PUSH_STRING, "Button")) return 0; }

            for (; a; a = a->next) {
                if (!compile_expression(compiler, a->node)) return 0;
                if (!emit_int(compiler, HVM_POP, 0)) return 0;
            }
            return emit_int(compiler, HVM_DRAW_BUTTON_STATE, 0);
        }

        for (i = 0; i < 4; i++) {
            if (a) { if (!compile_expression(compiler, a->node)) return 0; a = a->next; }
            else { if (!emit_int(compiler, HVM_PUSH_INT, 0)) return 0; }
        }
        if (a) { if (!compile_expression(compiler, a->node)) return 0; a = a->next; }
        else { if (!emit_str(compiler, HVM_PUSH_STRING, "")) return 0; }
        for (; a; a = a->next) { if (!compile_expression(compiler, a->node)) return 0; if (!emit_int(compiler, HVM_POP, 0)) return 0; }
        return emit_int(compiler, HVM_DRAW_BUTTON, 0);
    }

    if (strcmp(name, "input") == 0) {
        ASTNodeList *a = call_expr->data.call_expr.arguments;
        int argc = list_count(a);
        int i;

        if (argc <= 2) {
            if (a) { if (!compile_expression(compiler, a->node)) return 0; a = a->next; }
            else { if (!emit_str(compiler, HVM_PUSH_STRING, "input")) return 0; }

            if (a) { if (!compile_expression(compiler, a->node)) return 0; a = a->next; }
            else { if (!emit_str(compiler, HVM_PUSH_STRING, "")) return 0; }

            for (; a; a = a->next) {
                if (!compile_expression(compiler, a->node)) return 0;
                if (!emit_int(compiler, HVM_POP, 0)) return 0;
            }
            return emit_int(compiler, HVM_DRAW_INPUT_STATE, 0);
        }

        for (i = 0; i < 3; i++) {
            if (a) { if (!compile_expression(compiler, a->node)) return 0; a = a->next; }
            else { if (!emit_int(compiler, HVM_PUSH_INT, 0)) return 0; }
        }
        if (a) { if (!compile_expression(compiler, a->node)) return 0; a = a->next; }
        else { if (!emit_str(compiler, HVM_PUSH_STRING, "")) return 0; }
        for (; a; a = a->next) { if (!compile_expression(compiler, a->node)) return 0; if (!emit_int(compiler, HVM_POP, 0)) return 0; }
        return emit_int(compiler, HVM_DRAW_INPUT, 0);
    }

    if (strcmp(name, "textarea") == 0 || strcmp(name, "code_editor") == 0 || strcmp(name, "editor") == 0) {
        ASTNodeList *a = call_expr->data.call_expr.arguments; int i;
        for (i = 0; i < 4; i++) {
            if (a) { if (!compile_expression(compiler, a->node)) return 0; a = a->next; }
            else {
                if (i == 2) { if (!emit_int(compiler, HVM_PUSH_INT, 400)) return 0; }
                else if (i == 3) { if (!emit_int(compiler, HVM_PUSH_INT, 300)) return 0; }
                else { if (!emit_int(compiler, HVM_PUSH_INT, 0)) return 0; }
            }
        }
        if (a) { if (!compile_expression(compiler, a->node)) return 0; a = a->next; }
        else { if (!emit_str(compiler, HVM_PUSH_STRING, "main_textarea")) return 0; }
        for (; a; a = a->next) { if (!compile_expression(compiler, a->node)) return 0; if (!emit_int(compiler, HVM_POP, 0)) return 0; }
        return emit_int(compiler, HVM_DRAW_TEXTAREA, 0);
    }

    if (strcmp(name, "image") == 0 || strcmp(name, "sprite") == 0) {
        ASTNodeList *a = call_expr->data.call_expr.arguments; int i;
        for (i = 0; i < 4; i++) {
            if (a) { if (!compile_expression(compiler, a->node)) return 0; a = a->next; }
            else { if (!emit_int(compiler, HVM_PUSH_INT, 0)) return 0; }
        }
        if (a) { if (!compile_expression(compiler, a->node)) return 0; a = a->next; }
        else { if (!emit_str(compiler, HVM_PUSH_STRING, "[img]")) return 0; }
        for (; a; a = a->next) { if (!compile_expression(compiler, a->node)) return 0; if (!emit_int(compiler, HVM_POP, 0)) return 0; }
        return emit_int(compiler, HVM_DRAW_IMAGE, 0);
    }

    if (strcmp(name, "mouse_x") == 0) return emit_int(compiler, HVM_GET_MOUSE_X, 0);
    if (strcmp(name, "mouse_y") == 0) return emit_int(compiler, HVM_GET_MOUSE_Y, 0);
    if (strcmp(name, "mouse_down") == 0) return emit_int(compiler, HVM_IS_MOUSE_DOWN, 0);
    if (strcmp(name, "mouse_up") == 0) return emit_int(compiler, HVM_WAS_MOUSE_UP, 0);
    if (strcmp(name, "mouse_click") == 0 || strcmp(name, "mouse_clicked") == 0) return emit_int(compiler, HVM_WAS_MOUSE_CLICK, 0);

    if (strcmp(name, "mouse_hover") == 0) {
        arg_node = call_expr->data.call_expr.arguments;
        if (arg_node) { if (!compile_expression(compiler, arg_node->node)) return 0; arg_node = arg_node->next; }
        else { if (!emit_str(compiler, HVM_PUSH_STRING, "")) return 0; }
        for (; arg_node; arg_node = arg_node->next) { if (!compile_expression(compiler, arg_node->node)) return 0; if (!emit_int(compiler, HVM_POP, 0)) return 0; }
        return emit_int(compiler, HVM_IS_MOUSE_HOVER, 0);
    }

    if (strcmp(name, "key_down") == 0) {
        arg_node = call_expr->data.call_expr.arguments;
        if (arg_node) { if (!compile_expression(compiler, arg_node->node)) return 0; arg_node = arg_node->next; }
        else { if (!emit_int(compiler, HVM_PUSH_INT, 0)) return 0; }
        for (; arg_node; arg_node = arg_node->next) { if (!compile_expression(compiler, arg_node->node)) return 0; if (!emit_int(compiler, HVM_POP, 0)) return 0; }
        return emit_int(compiler, HVM_IS_KEY_DOWN, 0);
    }

    if (strcmp(name, "key_press") == 0 || strcmp(name, "key_pressed") == 0) {
        arg_node = call_expr->data.call_expr.arguments;
        if (arg_node) { if (!compile_expression(compiler, arg_node->node)) return 0; arg_node = arg_node->next; }
        else { if (!emit_int(compiler, HVM_PUSH_INT, 0)) return 0; }
        for (; arg_node; arg_node = arg_node->next) { if (!compile_expression(compiler, arg_node->node)) return 0; if (!emit_int(compiler, HVM_POP, 0)) return 0; }
        return emit_int(compiler, HVM_WAS_KEY_PRESS, 0);
    }

    if (strcmp(name, "delta") == 0 || strcmp(name, "dt") == 0) return emit_int(compiler, HVM_DELTA_TIME, 0);

    if (strcmp(name, "nl") == 0 || strcmp(name, "newline") == 0) {
        for (arg_node = call_expr->data.call_expr.arguments; arg_node; arg_node = arg_node->next) {
            if (!compile_expression(compiler, arg_node->node)) return 0;
            if (!emit_int(compiler, HVM_POP, 0)) return 0;
        }
        return emit_str(compiler, HVM_PUSH_STRING, "\n");
    }

    if (strcmp(name, "layout_reset") == 0) {
        ASTNodeList *a = call_expr->data.call_expr.arguments; int i;
        for (i = 0; i < 3; i++) {
            if (a) { if (!compile_expression(compiler, a->node)) return 0; a = a->next; }
            else { if (!emit_int(compiler, HVM_PUSH_INT, 0)) return 0; }
        }
        for (; a; a = a->next) { if (!compile_expression(compiler, a->node)) return 0; if (!emit_int(compiler, HVM_POP, 0)) return 0; }
        return emit_int(compiler, HVM_LAYOUT_RESET, 0);
    }

    if (strcmp(name, "layout_next") == 0) {
        arg_node = call_expr->data.call_expr.arguments;
        if (arg_node) { if (!compile_expression(compiler, arg_node->node)) return 0; arg_node = arg_node->next; }
        else { if (!emit_int(compiler, HVM_PUSH_INT, 24)) return 0; }
        for (; arg_node; arg_node = arg_node->next) { if (!compile_expression(compiler, arg_node->node)) return 0; if (!emit_int(compiler, HVM_POP, 0)) return 0; }
        return emit_int(compiler, HVM_LAYOUT_NEXT, 0);
    }

    if (strcmp(name, "layout_row") == 0) {
        arg_node = call_expr->data.call_expr.arguments;
        if (arg_node) { if (!compile_expression(compiler, arg_node->node)) return 0; arg_node = arg_node->next; }
        else { if (!emit_int(compiler, HVM_PUSH_INT, 1)) return 0; }
        for (; arg_node; arg_node = arg_node->next) { if (!compile_expression(compiler, arg_node->node)) return 0; if (!emit_int(compiler, HVM_POP, 0)) return 0; }
        return emit_int(compiler, HVM_LAYOUT_ROW, 0);
    }

    if (strcmp(name, "layout_column") == 0) {
        arg_node = call_expr->data.call_expr.arguments;
        if (arg_node) { if (!compile_expression(compiler, arg_node->node)) return 0; arg_node = arg_node->next; }
        else { if (!emit_int(compiler, HVM_PUSH_INT, 180)) return 0; }
        for (; arg_node; arg_node = arg_node->next) { if (!compile_expression(compiler, arg_node->node)) return 0; if (!emit_int(compiler, HVM_POP, 0)) return 0; }
        return emit_int(compiler, HVM_LAYOUT_COLUMN, 0);
    }

    if (strcmp(name, "layout_grid") == 0) {
        ASTNodeList *a = call_expr->data.call_expr.arguments;
        if (a) { if (!compile_expression(compiler, a->node)) return 0; a = a->next; }
        else { if (!emit_int(compiler, HVM_PUSH_INT, 2)) return 0; }
        if (a) { if (!compile_expression(compiler, a->node)) return 0; a = a->next; }
        else { if (!emit_int(compiler, HVM_PUSH_INT, 180)) return 0; }
        if (a) { if (!compile_expression(compiler, a->node)) return 0; a = a->next; }
        else { if (!emit_int(compiler, HVM_PUSH_INT, 32)) return 0; }
        for (; a; a = a->next) { if (!compile_expression(compiler, a->node)) return 0; if (!emit_int(compiler, HVM_POP, 0)) return 0; }
        return emit_int(compiler, HVM_LAYOUT_GRID, 0);
    }

    if (strcmp(name, "menu_notepad") == 0 || strcmp(name, "menu_setup") == 0) {
        for (arg_node = call_expr->data.call_expr.arguments; arg_node; arg_node = arg_node->next) {
            if (!compile_expression(compiler, arg_node->node)) return 0;
            if (!emit_int(compiler, HVM_POP, 0)) return 0;
        }
        return emit_int(compiler, HVM_MENU_SETUP_NOTEPAD, 0);
    }

    if (strcmp(name, "menu_event") == 0) {
        for (arg_node = call_expr->data.call_expr.arguments; arg_node; arg_node = arg_node->next) {
            if (!compile_expression(compiler, arg_node->node)) return 0;
            if (!emit_int(compiler, HVM_POP, 0)) return 0;
        }
        return emit_int(compiler, HVM_MENU_EVENT, 0);
    }

    if (strcmp(name, "scroll_range") == 0) {
        arg_node = call_expr->data.call_expr.arguments;
        if (arg_node) { if (!compile_expression(compiler, arg_node->node)) return 0; arg_node = arg_node->next; }
        else { if (!emit_int(compiler, HVM_PUSH_INT, 0)) return 0; }
        for (; arg_node; arg_node = arg_node->next) {
            if (!compile_expression(compiler, arg_node->node)) return 0;
            if (!emit_int(compiler, HVM_POP, 0)) return 0;
        }
        return emit_int(compiler, HVM_SCROLL_SET_RANGE, 0);
    }

    if (strcmp(name, "scroll_y") == 0) {
        for (arg_node = call_expr->data.call_expr.arguments; arg_node; arg_node = arg_node->next) {
            if (!compile_expression(compiler, arg_node->node)) return 0;
            if (!emit_int(compiler, HVM_POP, 0)) return 0;
        }
        return emit_int(compiler, HVM_SCROLL_Y, 0);
    }

    if (strcmp(name, "open_file_dialog") == 0 || strcmp(name, "open_file") == 0) {
        for (arg_node = call_expr->data.call_expr.arguments; arg_node; arg_node = arg_node->next) {
            if (!compile_expression(compiler, arg_node->node)) return 0;
            if (!emit_int(compiler, HVM_POP, 0)) return 0;
        }
        return emit_int(compiler, HVM_FILE_OPEN_DIALOG, 0);
    }

    if (strcmp(name, "save_file_dialog") == 0 || strcmp(name, "save_file") == 0) {
        for (arg_node = call_expr->data.call_expr.arguments; arg_node; arg_node = arg_node->next) {
            if (!compile_expression(compiler, arg_node->node)) return 0;
            if (!emit_int(compiler, HVM_POP, 0)) return 0;
        }
        return emit_int(compiler, HVM_FILE_SAVE_DIALOG, 0);
    }

    if (strcmp(name, "file_read") == 0) {
        arg_node = call_expr->data.call_expr.arguments;
        if (arg_node) { if (!compile_expression(compiler, arg_node->node)) return 0; arg_node = arg_node->next; }
        else { if (!emit_str(compiler, HVM_PUSH_STRING, "")) return 0; }
        for (; arg_node; arg_node = arg_node->next) {
            if (!compile_expression(compiler, arg_node->node)) return 0;
            if (!emit_int(compiler, HVM_POP, 0)) return 0;
        }
        return emit_int(compiler, HVM_FILE_READ, 0);
    }

    if (strcmp(name, "file_read_line") == 0) {
        ASTNodeList *a = call_expr->data.call_expr.arguments;
        if (a) { if (!compile_expression(compiler, a->node)) return 0; a = a->next; }
        else { if (!emit_str(compiler, HVM_PUSH_STRING, "")) return 0; }
        if (a) { if (!compile_expression(compiler, a->node)) return 0; a = a->next; }
        else { if (!emit_int(compiler, HVM_PUSH_INT, 1)) return 0; }
        for (; a; a = a->next) {
            if (!compile_expression(compiler, a->node)) return 0;
            if (!emit_int(compiler, HVM_POP, 0)) return 0;
        }
        return emit_int(compiler, HVM_FILE_READ_LINE, 0);
    }

    if (strcmp(name, "input_set") == 0 || strcmp(name, "set_input") == 0) {
        ASTNodeList *a = call_expr->data.call_expr.arguments;
        if (a) { if (!compile_expression(compiler, a->node)) return 0; a = a->next; }
        else { if (!emit_str(compiler, HVM_PUSH_STRING, "")) return 0; }
        if (a) { if (!compile_expression(compiler, a->node)) return 0; a = a->next; }
        else { if (!emit_str(compiler, HVM_PUSH_STRING, "")) return 0; }
        for (; a; a = a->next) {
            if (!compile_expression(compiler, a->node)) return 0;
            if (!emit_int(compiler, HVM_POP, 0)) return 0;
        }
        return emit_int(compiler, HVM_INPUT_SET, 0);
    }

    if (strcmp(name, "textarea_set") == 0 || strcmp(name, "editor_set_text") == 0 || strcmp(name, "code_editor_set") == 0) {
        ASTNodeList *a = call_expr->data.call_expr.arguments;
        if (a) { if (!compile_expression(compiler, a->node)) return 0; a = a->next; }
        else { if (!emit_str(compiler, HVM_PUSH_STRING, "main_textarea")) return 0; }
        if (a) { if (!compile_expression(compiler, a->node)) return 0; a = a->next; }
        else { if (!emit_str(compiler, HVM_PUSH_STRING, "")) return 0; }
        for (; a; a = a->next) {
            if (!compile_expression(compiler, a->node)) return 0;
            if (!emit_int(compiler, HVM_POP, 0)) return 0;
        }
        return emit_int(compiler, HVM_TEXTAREA_SET, 0);
    }

    if (strcmp(name, "file_write") == 0) {
        ASTNodeList *a = call_expr->data.call_expr.arguments;
        if (a) { if (!compile_expression(compiler, a->node)) return 0; a = a->next; }
        else { if (!emit_str(compiler, HVM_PUSH_STRING, "")) return 0; }
        if (a) { if (!compile_expression(compiler, a->node)) return 0; a = a->next; }
        else { if (!emit_str(compiler, HVM_PUSH_STRING, "")) return 0; }
        for (; a; a = a->next) {
            if (!compile_expression(compiler, a->node)) return 0;
            if (!emit_int(compiler, HVM_POP, 0)) return 0;
        }
        return emit_int(compiler, HVM_FILE_WRITE, 0);
    }

    if (strcmp(name, "exec") == 0 || strcmp(name, "sys_exec") == 0 || strcmp(name, "run_shell") == 0) {
        arg_node = call_expr->data.call_expr.arguments;
        if (arg_node) { if (!compile_expression(compiler, arg_node->node)) return 0; arg_node = arg_node->next; }
        else { if (!emit_str(compiler, HVM_PUSH_STRING, "")) return 0; }
        for (; arg_node; arg_node = arg_node->next) {
            if (!compile_expression(compiler, arg_node->node)) return 0;
            if (!emit_int(compiler, HVM_POP, 0)) return 0;
        }
        return emit_int(compiler, HVM_EXEC_COMMAND, 0);
    }

    fn_index = find_function_index(compiler, name);
    if (fn_index < 0) {
        char msg[256];
        snprintf(msg, sizeof(msg), "Unknown function/builtin: %s", name ? name : "<null>");
        hvm_compiler_add_error(compiler, msg);
        return 0;
    }

    {
        FunctionEntry *target = &internal->functions[(size_t)fn_index];
        ASTNode *fn_ast = target->ast;
        size_t param_count = fn_ast->data.function.param_count;
        size_t arg_i = 0;
        ASTNodeList *a = call_expr->data.call_expr.arguments;

        for (arg_i = 0; arg_i < param_count; arg_i++) {
            if (a) {
                if (!compile_expression(compiler, a->node)) return 0;
                a = a->next;
            } else {
                if (!emit_int(compiler, HVM_PUSH_INT, 0)) return 0;
            }
        }

        while (a) {
            if (!compile_expression(compiler, a->node)) return 0;
            if (!emit_int(compiler, HVM_POP, 0)) return 0;
            a = a->next;
        }

        {
            size_t call_index = compiler->vm->instruction_count;
            if (!emit_int(compiler, HVM_CALL, 0)) return 0;
            if (!add_call_patch(compiler, call_index, (size_t)fn_index)) return 0;
        }
    }

    return 1;
}

static int compile_expression(HVM_Compiler* compiler, ASTNode* ast) {
    const char *resolved;
    if (!compiler || !ast) return 0;

    switch (ast->type) {
        case AST_NUMBER:
            return emit_int(compiler, HVM_PUSH_INT, ast->data.number.value);
        case AST_FLOAT:
            return emit_float(compiler, HVM_PUSH_FLOAT, ast->data.fnumber.value);
        case AST_BOOL:
            return emit_int(compiler, HVM_PUSH_BOOL, ast->data.boolean.value);
        case AST_STRING:
            return emit_str(compiler, HVM_PUSH_STRING, ast->data.string_lit.value);
        case AST_IDENTIFIER:
            resolved = find_local_binding(compiler, ast->data.identifier.name);
            if (!resolved) resolved = ast->data.identifier.name;
            return emit_str(compiler, HVM_LOAD_GLOBAL, resolved);
        case AST_CALL_EXPR:
            return compile_call_expression(compiler, ast);
        case AST_UNARY_OP:
            switch (ast->data.unary_op.op) {
                case TOKEN_BANG:
                    if (!compile_expression(compiler, ast->data.unary_op.operand)) return 0;
                    return emit_int(compiler, HVM_NOT, 0);
                case TOKEN_MINUS:
                    if (!emit_int(compiler, HVM_PUSH_INT, 0)) return 0;
                    if (!compile_expression(compiler, ast->data.unary_op.operand)) return 0;
                    return emit_int(compiler, HVM_SUB, 0);
                default:
                    hvm_compiler_add_error(compiler, "Unsupported unary operator");
                    return 0;
            }
        case AST_BINARY_OP:
            if (!compile_expression(compiler, ast->data.binary_op.left)) return 0;
            if (!compile_expression(compiler, ast->data.binary_op.right)) return 0;
            switch (ast->data.binary_op.op) {
                case TOKEN_PLUS: return emit_int(compiler, HVM_ADD, 0);
                case TOKEN_MINUS: return emit_int(compiler, HVM_SUB, 0);
                case TOKEN_STAR: return emit_int(compiler, HVM_MUL, 0);
                case TOKEN_SLASH: return emit_int(compiler, HVM_DIV, 0);
                case TOKEN_PERCENT: return emit_int(compiler, HVM_MOD, 0);
                case TOKEN_AND: return emit_int(compiler, HVM_AND, 0);
                case TOKEN_OR: return emit_int(compiler, HVM_OR, 0);
                case TOKEN_EQUAL_EQUAL: return emit_int(compiler, HVM_EQ, 0);
                case TOKEN_BANG_EQUAL: return emit_int(compiler, HVM_NE, 0);
                case TOKEN_LESS: return emit_int(compiler, HVM_LT, 0);
                case TOKEN_LESS_EQUAL: return emit_int(compiler, HVM_LE, 0);
                case TOKEN_GREATER: return emit_int(compiler, HVM_GT, 0);
                case TOKEN_GREATER_EQUAL: return emit_int(compiler, HVM_GE, 0);
                default:
                    hvm_compiler_add_error(compiler, "Unsupported binary operator");
                    return 0;
            }
        default:
            hvm_compiler_add_error(compiler, "Unsupported expression");
            return 0;
    }
}

static int compile_function(HVM_Compiler* compiler, FunctionEntry *fn_entry) {
    CompilerInternal *internal = ci(compiler);
    ASTNode *fn;
    size_t i;

    if (!internal || !fn_entry || !fn_entry->ast) return 0;
    fn = fn_entry->ast;

    fn_entry->entry = compiler->vm->instruction_count;
    fn_entry->entry_set = 1;

    internal->current_function = fn_entry->name;
    clear_local_bindings(compiler);

    for (i = 0; i < fn->data.function.param_count; i++) {
        if (!add_local_binding(compiler, fn->data.function.params[i], NULL)) {
            hvm_compiler_add_error(compiler, "Out of memory while binding function parameters");
            clear_local_bindings(compiler);
            internal->current_function = NULL;
            return 0;
        }
    }

    for (i = fn->data.function.param_count; i > 0; i--) {
        const char *scoped_param = find_local_binding(compiler, fn->data.function.params[i - 1]);
        if (!scoped_param) {
            clear_local_bindings(compiler);
            internal->current_function = NULL;
            return 0;
        }
        if (!emit_str(compiler, HVM_STORE_GLOBAL, scoped_param)) {
            clear_local_bindings(compiler);
            internal->current_function = NULL;
            return 0;
        }
    }

    if (!compile_block(compiler, fn->data.function.body)) {
        clear_local_bindings(compiler);
        internal->current_function = NULL;
        return 0;
    }

    if (compiler->vm->instruction_count == 0 ||
        compiler->vm->instructions[compiler->vm->instruction_count - 1].opcode != HVM_RETURN) {
        if (!emit_int(compiler, HVM_PUSH_INT, 0)) {
            clear_local_bindings(compiler);
            internal->current_function = NULL;
            return 0;
        }
        if (!emit_int(compiler, HVM_RETURN, 0)) {
            clear_local_bindings(compiler);
            internal->current_function = NULL;
            return 0;
        }
    }

    clear_local_bindings(compiler);
    internal->current_function = NULL;
    return 1;
}

static int compile_statement(HVM_Compiler* compiler, ASTNode* ast) {
    CompilerInternal *internal = ci(compiler);
    if (!compiler || !ast) return 0;

    switch (ast->type) {
        case AST_BLOCK:
            return compile_block(compiler, ast);

        case AST_PACKAGE:
        case AST_IMPORT:
            return 1;

        case AST_VARIABLE_DECLARATION: {
            const char *target_name = ast->data.variable_declaration.identifier;
            if (internal && internal->current_function) {
                if (!add_local_binding(compiler, ast->data.variable_declaration.identifier, &target_name)) {
                    hvm_compiler_add_error(compiler, "Out of memory while declaring local variable");
                    return 0;
                }
            }
            if (!compile_expression(compiler, ast->data.variable_declaration.value)) return 0;
            return emit_str(compiler, HVM_STORE_GLOBAL, target_name);
        }

        case AST_ASSIGNMENT: {
            const char *target_name = ast->data.assignment.identifier;
            const char *local_name = find_local_binding(compiler, ast->data.assignment.identifier);
            if (local_name) target_name = local_name;
            if (!compile_expression(compiler, ast->data.assignment.value)) return 0;
            return emit_str(compiler, HVM_STORE_GLOBAL, target_name);
        }

        case AST_PRINT_STATEMENT:
            if (!compile_expression(compiler, ast->data.print_statement.expression)) return 0;
            return emit_int(compiler, HVM_PRINTLN, 0);

        case AST_EXPR_STATEMENT:
            if (!compile_expression(compiler, ast->data.expr_stmt.expression)) return 0;
            return emit_int(compiler, HVM_POP, 0);

        case AST_WINDOW_STMT:
            return emit_str(compiler, HVM_CREATE_WINDOW, ast->data.window_stmt.title);

        case AST_TEXT_STMT:
            if (!compile_expression(compiler, ast->data.text_stmt.x)) return 0;
            if (!compile_expression(compiler, ast->data.text_stmt.y)) return 0;
            if (!emit_str(compiler, HVM_PUSH_STRING, ast->data.text_stmt.msg)) return 0;
            return emit_int(compiler, HVM_DRAW_TEXT, 0);

        case AST_IF: {
            int jmp_false;
            if (!compile_expression(compiler, ast->data.if_stmt.condition)) return 0;
            jmp_false = emit_jump(compiler, HVM_JUMP_IF_FALSE);
            if (jmp_false < 0) return 0;

            if (!compile_statement(compiler, ast->data.if_stmt.then_branch)) return 0;

            if (ast->data.if_stmt.else_branch) {
                int jmp_end = emit_jump(compiler, HVM_JUMP);
                patch_jump(compiler, jmp_false, compiler->vm->instruction_count);
                if (!compile_statement(compiler, ast->data.if_stmt.else_branch)) return 0;
                patch_jump(compiler, jmp_end, compiler->vm->instruction_count);
            } else {
                patch_jump(compiler, jmp_false, compiler->vm->instruction_count);
            }
            return 1;
        }

        case AST_WHILE: {
            size_t loop_start = compiler->vm->instruction_count;
            int jmp_exit;
            int ok = 0;

            if (!compile_expression(compiler, ast->data.while_stmt.condition)) return 0;
            jmp_exit = emit_jump(compiler, HVM_JUMP_IF_FALSE);
            if (jmp_exit < 0) return 0;

            if (!push_loop_context(compiler, loop_start, 1)) {
                hvm_compiler_add_error(compiler, "Out of memory while tracking loop state");
                return 0;
            }

            if (!compile_statement(compiler, ast->data.while_stmt.body)) goto while_cleanup;
            if (!emit_int(compiler, HVM_JUMP, (int64_t)loop_start)) goto while_cleanup;

            patch_jump(compiler, jmp_exit, compiler->vm->instruction_count);
            patch_loop_breaks(compiler, compiler->vm->instruction_count);
            set_loop_continue_target(compiler, loop_start);
            ok = 1;

while_cleanup:
            pop_loop_context(compiler);
            return ok;
        }

        case AST_FOR: {
            size_t loop_start;
            size_t update_start;
            int jmp_exit;
            int ok = 0;

            if (ast->data.for_stmt.init) {
                if (!compile_statement(compiler, ast->data.for_stmt.init)) return 0;
            }

            loop_start = compiler->vm->instruction_count;
            if (ast->data.for_stmt.condition) {
                if (!compile_expression(compiler, ast->data.for_stmt.condition)) return 0;
            } else {
                if (!emit_int(compiler, HVM_PUSH_BOOL, 1)) return 0;
            }

            jmp_exit = emit_jump(compiler, HVM_JUMP_IF_FALSE);
            if (jmp_exit < 0) return 0;

            if (!push_loop_context(compiler, 0, 0)) {
                hvm_compiler_add_error(compiler, "Out of memory while tracking loop state");
                return 0;
            }

            if (!compile_statement(compiler, ast->data.for_stmt.body)) goto for_cleanup;

            update_start = compiler->vm->instruction_count;
            set_loop_continue_target(compiler, update_start);

            if (ast->data.for_stmt.update) {
                if (!compile_statement(compiler, ast->data.for_stmt.update)) goto for_cleanup;
            }

            if (!emit_int(compiler, HVM_JUMP, (int64_t)loop_start)) goto for_cleanup;

            patch_jump(compiler, jmp_exit, compiler->vm->instruction_count);
            patch_loop_breaks(compiler, compiler->vm->instruction_count);
            ok = 1;

for_cleanup:
            pop_loop_context(compiler);
            return ok;
        }

        case AST_BREAK: {
            int j = emit_jump(compiler, HVM_JUMP);
            if (j < 0) return 0;
            if (!add_loop_patch(compiler, j, 0)) {
                hvm_compiler_add_error(compiler, "break used outside loop");
                return 0;
            }
            return 1;
        }

        case AST_CONTINUE: {
            int j = emit_jump(compiler, HVM_JUMP);
            if (j < 0) return 0;
            if (!add_loop_patch(compiler, j, 1)) {
                hvm_compiler_add_error(compiler, "continue used outside loop");
                return 0;
            }
            return 1;
        }

        case AST_RETURN:
            if (ast->data.return_stmt.value) {
                if (!compile_expression(compiler, ast->data.return_stmt.value)) return 0;
            } else {
                if (!emit_int(compiler, HVM_PUSH_INT, 0)) return 0;
            }
            if (internal && internal->current_function) return emit_int(compiler, HVM_RETURN, 0);
            return emit_int(compiler, HVM_HALT, 0);

        case AST_FUNCTION: {
            int index = find_function_index(compiler, ast->data.function.name);
            if (index < 0) return 1;
            return compile_function(compiler, &ci(compiler)->functions[(size_t)index]);
        }

        default:
            hvm_compiler_add_error(compiler, "Unsupported statement");
            return 0;
    }
}

static int compile_block(HVM_Compiler* compiler, ASTNode* ast) {
    ASTNodeList *cur;
    if (!ast || ast->type != AST_BLOCK) return 0;
    for (cur = ast->data.block.statements; cur; cur = cur->next) {
        if (!cur->node) continue;
        if (!compile_statement(compiler, cur->node)) return 0;
    }
    return 1;
}

int hvm_compile_program(HVM_Compiler* compiler, ASTNode* ast) {
    CompilerInternal *internal = ci(compiler);
    ASTNodeList *cur;
    int main_index;
    size_t i;

    if (!compiler || !ast || !internal) return 0;
    if (ast->type != AST_PROGRAM) return compile_statement(compiler, ast);

    reset_internal_state(compiler);

    for (cur = ast->data.program.declarations; cur; cur = cur->next) {
        if (cur->node && cur->node->type == AST_FUNCTION) {
            if (!register_function(compiler, cur->node)) return 0;
        }
    }

    for (cur = ast->data.program.declarations; cur; cur = cur->next) {
        if (!cur->node || cur->node->type == AST_FUNCTION) continue;
        if (!compile_statement(compiler, cur->node)) return 0;
    }

    main_index = find_function_index(compiler, "main");
    if (main_index >= 0) {
        size_t call_index = compiler->vm->instruction_count;
        if (!emit_int(compiler, HVM_CALL, 0)) return 0;
        if (!add_call_patch(compiler, call_index, (size_t)main_index)) return 0;
        if (!emit_int(compiler, HVM_POP, 0)) return 0;
    }

    if (!emit_int(compiler, HVM_HALT, 0)) return 0;

    for (i = 0; i < internal->function_count; i++) {
        if (!compile_function(compiler, &internal->functions[i])) return 0;
    }

    for (i = 0; i < internal->call_patch_count; i++) {
        CallPatch *patch = &internal->call_patches[i];
        if (patch->function_index >= internal->function_count) {
            hvm_compiler_add_error(compiler, "Invalid function call patch");
            return 0;
        }
        if (!internal->functions[patch->function_index].entry_set) {
            hvm_compiler_add_error(compiler, "Unresolved function call target");
            return 0;
        }
        if (patch->instruction_index >= compiler->vm->instruction_count) {
            hvm_compiler_add_error(compiler, "Invalid instruction index for call patch");
            return 0;
        }
        compiler->vm->instructions[patch->instruction_index].operand.int_operand = (int64_t)internal->functions[patch->function_index].entry;
    }

    return 1;
}

int hvm_compile_statement(HVM_Compiler* compiler, ASTNode* ast) { return compile_statement(compiler, ast); }
int hvm_compile_expression(HVM_Compiler* compiler, ASTNode* ast) { return compile_expression(compiler, ast); }
int hvm_compile_variable_declaration(HVM_Compiler* compiler, ASTNode* ast) { return compile_statement(compiler, ast); }
int hvm_compile_number_literal(HVM_Compiler* compiler, ASTNode* ast) { return compile_expression(compiler, ast); }
int hvm_compile_print_statement(HVM_Compiler* compiler, ASTNode* ast) { return compile_statement(compiler, ast); }
int hvm_compile_win32_message_box(HVM_Compiler* compiler, ASTNode* ast) { (void)compiler; (void)ast; return 0; }
int hvm_compile_win32_error(HVM_Compiler* compiler, ASTNode* ast) { (void)compiler; (void)ast; return 0; }
int hvm_compile_win32_info(HVM_Compiler* compiler, ASTNode* ast) { (void)compiler; (void)ast; return 0; }
int hvm_compile_win32_warning(HVM_Compiler* compiler, ASTNode* ast) { (void)compiler; (void)ast; return 0; }
int hvm_compile_sleep(HVM_Compiler* compiler, ASTNode* ast) { (void)compiler; (void)ast; return 0; }
int hvm_compile_beep(HVM_Compiler* compiler, ASTNode* ast) { (void)compiler; (void)ast; return 0; }

int hvm_compiler_compile_ast(HVM_Compiler* compiler, ASTNode* ast) {
    if (!compiler || !ast) return 0;
    return hvm_compile_program(compiler, ast);
}

int hvm_compiler_has_errors(HVM_Compiler* compiler) { return compiler ? compiler->error_count > 0 : 0; }

void hvm_compiler_print_errors(HVM_Compiler* compiler) {
    int i;
    if (!compiler || !compiler->error_count) return;
    printf("Compiler Errors:\n");
    for (i = 0; i < compiler->error_count; i++) {
        printf("  %d: %s\n", i + 1, compiler->error_messages[i]);
    }
}












