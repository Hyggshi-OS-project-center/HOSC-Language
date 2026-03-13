/*
 * File: compiler\src\hosc_compiler.c
 * Purpose: HOSC source file.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <ctype.h>
#include <errno.h>
#include "parser.h"
#include "codegen.h"
#include "hvm.h"
#include "hvm_compiler.h"

#define MAGIC "HBC1"

typedef enum { OP_NONE=0, OP_INT=1, OP_FLOAT=2, OP_STRING=3 } OpKind;

typedef struct {
    char *data;
    size_t len;
    size_t cap;
} StrBuf;

typedef struct {
    char **items;
    size_t count;
    size_t cap;
} StrList;

typedef struct {
    int matched;
    int is_quoted;
    int is_from;
    char *module;
    char *alias;
    char *rewrite_prefix;
} ImportDirective;

typedef struct {
    StrList loaded;
    StrList active;
} ImportContext;

static char *dup_cstr(const char *s) {
    size_t len;
    char *out;
    if (!s) return NULL;
    len = strlen(s);
    out = (char *)malloc(len + 1);
    if (!out) return NULL;
    memcpy(out, s, len + 1);
    return out;
}

static char *dup_range(const char *s, size_t len) {
    char *out = (char *)malloc(len + 1);
    if (!out) return NULL;
    memcpy(out, s, len);
    out[len] = '\0';
    return out;
}

static void sb_init(StrBuf *b) {
    if (!b) return;
    b->data = NULL;
    b->len = 0;
    b->cap = 0;
}

static void sb_free(StrBuf *b) {
    if (!b) return;
    free(b->data);
    b->data = NULL;
    b->len = 0;
    b->cap = 0;
}

static int sb_reserve(StrBuf *b, size_t need_extra) {
    size_t need;
    size_t new_cap;
    char *n;
    if (!b) return 0;
    need = b->len + need_extra + 1;
    if (need <= b->cap) return 1;
    new_cap = b->cap ? b->cap : 128;
    while (new_cap < need) new_cap *= 2;
    n = (char *)realloc(b->data, new_cap);
    if (!n) return 0;
    b->data = n;
    b->cap = new_cap;
    return 1;
}

static int sb_append_n(StrBuf *b, const char *s, size_t n) {
    if (!b || (!s && n > 0)) return 0;
    if (!sb_reserve(b, n)) return 0;
    if (n > 0) memcpy(b->data + b->len, s, n);
    b->len += n;
    b->data[b->len] = '\0';
    return 1;
}

static int sb_append(StrBuf *b, const char *s) {
    if (!s) return 1;
    return sb_append_n(b, s, strlen(s));
}

static char *sb_steal(StrBuf *b) {
    char *out;
    if (!b) return NULL;
    if (!b->data) {
        out = (char *)malloc(1);
        if (!out) return NULL;
        out[0] = '\0';
        return out;
    }
    out = b->data;
    b->data = NULL;
    b->len = 0;
    b->cap = 0;
    return out;
}

static void strlist_free(StrList *l) {
    size_t i;
    if (!l) return;
    for (i = 0; i < l->count; i++) {
        free(l->items[i]);
    }
    free(l->items);
    l->items = NULL;
    l->count = 0;
    l->cap = 0;
}

static int strlist_contains(const StrList *l, const char *s) {
    size_t i;
    if (!l || !s) return 0;
    for (i = 0; i < l->count; i++) {
        if (l->items[i] && strcmp(l->items[i], s) == 0) return 1;
    }
    return 0;
}

static int strlist_push(StrList *l, const char *s) {
    char *copy;
    char **nitems;
    size_t new_cap;
    if (!l || !s) return 0;
    if (l->count == l->cap) {
        new_cap = l->cap ? l->cap * 2 : 8;
        nitems = (char **)realloc(l->items, sizeof(char *) * new_cap);
        if (!nitems) return 0;
        l->items = nitems;
        l->cap = new_cap;
    }
    copy = dup_cstr(s);
    if (!copy) return 0;
    l->items[l->count++] = copy;
    return 1;
}

static int strlist_push_unique(StrList *l, const char *s) {
    if (strlist_contains(l, s)) return 1;
    return strlist_push(l, s);
}

static void strlist_pop(StrList *l) {
    if (!l || l->count == 0) return;
    free(l->items[l->count - 1]);
    l->items[l->count - 1] = NULL;
    l->count--;
}

static int is_ident_start_char(char c) {
    return isalpha((unsigned char)c) || c == '_';
}

static int is_ident_char(char c) {
    return isalnum((unsigned char)c) || c == '_';
}

static const char *skip_spaces(const char *s) {
    if (!s) return s;
    if ((unsigned char)s[0] == 0xEF && (unsigned char)s[1] == 0xBB && (unsigned char)s[2] == 0xBF) {
        s += 3;
    }
    while (*s && isspace((unsigned char)*s)) s++;
    return s;
}

static int starts_with_kw(const char *s, const char *kw) {
    size_t n = strlen(kw);
    if (strncmp(s, kw, n) != 0) return 0;
    if (s[n] == '\0') return 1;
    return isspace((unsigned char)s[n]);
}

static int parse_ident_path_token(const char **pp, char **out) {
    const char *p = *pp;
    const char *start;

    if (!p || !out) return 0;
    if (!is_ident_start_char(*p)) return 0;
    start = p;
    while (*p && (is_ident_char(*p) || *p == '.' || *p == '/' || *p == '\\')) p++;
    *out = dup_range(start, (size_t)(p - start));
    if (!*out) return 0;
    *pp = p;
    return 1;
}

static int parse_alias_suffix(const char *p, char **alias_out) {
    if (!p || !alias_out) return 1;
    p = skip_spaces(p);
    if (*p == ';') p++;
    p = skip_spaces(p);

    if (*p == '\0') return 1;
    if (p[0] == '/' && p[1] == '/') return 1;

    if (strncmp(p, "as", 2) == 0 && isspace((unsigned char)p[2])) {
        const char *q = skip_spaces(p + 2);
        const char *start;
        if (!is_ident_start_char(*q)) return 0;
        start = q;
        while (*q && is_ident_char(*q)) q++;
        *alias_out = dup_range(start, (size_t)(q - start));
        if (!*alias_out) return 0;
        q = skip_spaces(q);
        if (*q == ';') q++;
        q = skip_spaces(q);
        if (*q == '\0' || (q[0] == '/' && q[1] == '/')) return 1;
        return 0;
    }

    return 0;
}

static void import_directive_free(ImportDirective *d) {
    if (!d) return;
    free(d->module);
    free(d->alias);
    free(d->rewrite_prefix);
    memset(d, 0, sizeof(*d));
}

static int parse_import_directive(const char *line, ImportDirective *d) {
    const char *p;
    memset(d, 0, sizeof(*d));
    if (!line) return 0;

    p = skip_spaces(line);
    if (*p == '\0') return 0;
    if (p[0] == '/' && p[1] == '/') return 0;

    if (starts_with_kw(p, "import")) {
        p = skip_spaces(p + 6);
        d->matched = 1;
        d->is_from = 0;

        if (*p == '"') {
            const char *start;
            p++;
            start = p;
            while (*p && *p != '"') p++;
            if (*p != '"') return 0;
            d->module = dup_range(start, (size_t)(p - start));
            if (!d->module) return 0;
            d->is_quoted = 1;
            p++;
            if (!parse_alias_suffix(p, &d->alias)) return 0;
            if (d->alias) d->rewrite_prefix = dup_cstr(d->alias);
            return 1;
        }

        if (!parse_ident_path_token(&p, &d->module)) return 0;
        d->is_quoted = 0;
        if (!parse_alias_suffix(p, &d->alias)) return 0;
        if (d->alias) d->rewrite_prefix = dup_cstr(d->alias);
        else d->rewrite_prefix = dup_cstr(d->module);
        if (!d->rewrite_prefix) return 0;
        return 1;
    }

    if (starts_with_kw(p, "from")) {
        p = skip_spaces(p + 4);
        d->matched = 1;
        d->is_from = 1;

        if (!parse_ident_path_token(&p, &d->module)) return 0;
        p = skip_spaces(p);
        if (!starts_with_kw(p, "import")) return 0;
        p = skip_spaces(p + 6);

        if (*p == '*') {
            p++;
        } else {
            while (1) {
                char *name = NULL;
                if (!parse_ident_path_token(&p, &name)) return 0;
                free(name);
                p = skip_spaces(p);
                if (*p != ',') break;
                p++;
                p = skip_spaces(p);
            }
        }

        p = skip_spaces(p);
        if (*p == ';') p++;
        p = skip_spaces(p);
        if (*p != '\0' && !(p[0] == '/' && p[1] == '/')) return 0;

        d->is_quoted = 0;
        return 1;
    }

    return 0;
}

static int file_exists(const char *path) {
    FILE *fp;
    if (!path || !path[0]) return 0;
    fp = fopen(path, "rb");
    if (!fp) return 0;
    fclose(fp);
    return 1;
}

static void get_dirname(const char *path, char *out, size_t out_size) {
    const char *s1;
    const char *s2;
    const char *slash;
    size_t len;

    if (!out || out_size == 0) return;
    out[0] = '\0';
    if (!path) return;

    s1 = strrchr(path, '\\');
    s2 = strrchr(path, '/');
    slash = (s1 && s2) ? ((s1 > s2) ? s1 : s2) : (s1 ? s1 : s2);
    if (!slash) return;

    len = (size_t)(slash - path);
    if (len >= out_size) len = out_size - 1;
    memcpy(out, path, len);
    out[len] = '\0';
}

static int path_is_absolute(const char *path) {
    if (!path || !path[0]) return 0;
    if ((isalpha((unsigned char)path[0]) && path[1] == ':') ||
        (path[0] == '\\' && path[1] == '\\') ||
        path[0] == '/' || path[0] == '\\') {
        return 1;
    }
    return 0;
}

static int path_join(const char *dir, const char *name, char *out, size_t out_size) {
    int written;
    if (!out || out_size == 0) return 0;
    out[0] = '\0';
    if (!dir || !dir[0]) {
        written = snprintf(out, out_size, "%s", name ? name : "");
    } else {
        written = snprintf(out, out_size, "%s/%s", dir, name ? name : "");
    }
    return written >= 0 && (size_t)written < out_size;
}

static char *normalize_path(const char *path) {
    char resolved[4096];
    if (!_fullpath(resolved, path, sizeof(resolved))) return NULL;
    return dup_cstr(resolved);
}

static int has_extension(const char *path) {
    const char *slash1;
    const char *slash2;
    const char *slash;
    const char *dot;
    if (!path) return 0;
    slash1 = strrchr(path, '/');
    slash2 = strrchr(path, '\\');
    slash = (slash1 && slash2) ? ((slash1 > slash2) ? slash1 : slash2) : (slash1 ? slash1 : slash2);
    dot = strrchr(path, '.');
    return dot && (!slash || dot > slash);
}

static char *module_to_path(const char *module, int is_quoted) {
    size_t len;
    size_t i;
    StrBuf b;

    if (!module) return NULL;
    if (is_quoted) {
        if (has_extension(module)) return dup_cstr(module);
        len = strlen(module);
        b.data = NULL;
        b.len = 0;
        b.cap = 0;
        if (!sb_append_n(&b, module, len)) return NULL;
        if (!sb_append(&b, ".hosc")) {
            sb_free(&b);
            return NULL;
        }
        return sb_steal(&b);
    }

    len = strlen(module);
    sb_init(&b);
    for (i = 0; i < len; i++) {
        char c = module[i];
        if (c == '.') c = '/';
        if (!sb_append_n(&b, &c, 1)) {
            sb_free(&b);
            return NULL;
        }
    }
    if (!has_extension(b.data ? b.data : "")) {
        if (!sb_append(&b, ".hosc")) {
            sb_free(&b);
            return NULL;
        }
    }
    return sb_steal(&b);
}

static char *resolve_import_path(const char *current_abs, const ImportDirective *d) {
    char dir[4096];
    char joined[4096];
    char *mod_path;
    char *norm;

    if (!current_abs || !d || !d->module) return NULL;
    mod_path = module_to_path(d->module, d->is_quoted);
    if (!mod_path) return NULL;

    if (path_is_absolute(mod_path)) {
        norm = normalize_path(mod_path);
        free(mod_path);
        return norm;
    }

    get_dirname(current_abs, dir, sizeof(dir));
    if (!path_join(dir, mod_path, joined, sizeof(joined))) {
        free(mod_path);
        return NULL;
    }
    free(mod_path);
    return normalize_path(joined);
}

static char *read_file(const char *path) {
    FILE *fp;
    long size;
    char *buffer;
    fp = fopen(path, "rb");
    if (!fp) return NULL;
    if (fseek(fp, 0, SEEK_END) != 0) { fclose(fp); return NULL; }
    size = ftell(fp);
    if (size < 0) { fclose(fp); return NULL; }
    rewind(fp);
    buffer = (char *)malloc((size_t)size + 1);
    if (!buffer) { fclose(fp); return NULL; }
    if (fread(buffer, 1, (size_t)size, fp) != (size_t)size) { free(buffer); fclose(fp); return NULL; }
    buffer[size] = '\0';
    fclose(fp);
    return buffer;
}

static char *rewrite_prefixed_calls(const char *text, const StrList *prefixes) {
    enum { ST_NORMAL, ST_STRING, ST_LINE_COMMENT, ST_BLOCK_COMMENT } st = ST_NORMAL;
    size_t i = 0;
    StrBuf out;

    sb_init(&out);
    if (!text) {
        return sb_steal(&out);
    }

    while (text[i]) {
        char c = text[i];
        if (st == ST_NORMAL) {
            size_t k;
            int removed = 0;

            if (c == '"') {
                sb_append_n(&out, &c, 1);
                st = ST_STRING;
                i++;
                continue;
            }
            if (c == '/' && text[i + 1] == '/') {
                sb_append_n(&out, text + i, 2);
                st = ST_LINE_COMMENT;
                i += 2;
                continue;
            }
            if (c == '/' && text[i + 1] == '*') {
                sb_append_n(&out, text + i, 2);
                st = ST_BLOCK_COMMENT;
                i += 2;
                continue;
            }

            for (k = 0; prefixes && k < prefixes->count; k++) {
                const char *p = prefixes->items[k];
                size_t plen;
                if (!p || !p[0]) continue;
                plen = strlen(p);
                if (strncmp(text + i, p, plen) == 0 && text[i + plen] == '.') {
                    int before_ok = (i == 0) || !is_ident_char(text[i - 1]);
                    int after_ok = is_ident_start_char(text[i + plen + 1]);
                    if (before_ok && after_ok) {
                        i += plen + 1;
                        removed = 1;
                        break;
                    }
                }
            }
            if (removed) continue;

            sb_append_n(&out, &c, 1);
            i++;
            continue;
        }

        if (st == ST_STRING) {
            sb_append_n(&out, &c, 1);
            if (c == '\\' && text[i + 1]) {
                sb_append_n(&out, text + i + 1, 1);
                i += 2;
                continue;
            }
            if (c == '"') st = ST_NORMAL;
            i++;
            continue;
        }

        if (st == ST_LINE_COMMENT) {
            sb_append_n(&out, &c, 1);
            if (c == '\n') st = ST_NORMAL;
            i++;
            continue;
        }

        sb_append_n(&out, &c, 1);
        if (c == '*' && text[i + 1] == '/') {
            sb_append_n(&out, text + i + 1, 1);
            i += 2;
            st = ST_NORMAL;
            continue;
        }
        i++;
    }

    return sb_steal(&out);
}

static char *resolve_imports_recursive(const char *path, ImportContext *ctx, char **err_msg) {
    char *abs_path;
    char *source;
    const char *cursor;
    StrBuf import_buf;
    StrBuf package_buf;
    StrBuf body_buf;
    StrList rewrite_prefixes = {0};
    char *rewritten_body = NULL;
    char *final_text = NULL;
    int package_taken = 0;
    int is_entry = 0;

    if (!path || !ctx) return NULL;

    abs_path = normalize_path(path);
    if (!abs_path) {
        if (err_msg) *err_msg = dup_cstr("cannot normalize import path");
        return NULL;
    }

    if (strlist_contains(&ctx->loaded, abs_path)) {
        free(abs_path);
        return dup_cstr("");
    }

    if (strlist_contains(&ctx->active, abs_path)) {
        if (err_msg) {
            StrBuf eb;
            sb_init(&eb);
            sb_append(&eb, "circular import detected: ");
            sb_append(&eb, abs_path);
            *err_msg = sb_steal(&eb);
        }
        free(abs_path);
        return NULL;
    }

    if (!file_exists(abs_path)) {
        if (err_msg) {
            StrBuf eb;
            sb_init(&eb);
            sb_append(&eb, "import file not found: ");
            sb_append(&eb, abs_path);
            *err_msg = sb_steal(&eb);
        }
        free(abs_path);
        return NULL;
    }

    if (!strlist_push(&ctx->active, abs_path)) {
        free(abs_path);
        if (err_msg) *err_msg = dup_cstr("out of memory (active import stack)");
        return NULL;
    }

    is_entry = (ctx->active.count == 1);

    source = read_file(abs_path);
    if (!source) {
        strlist_pop(&ctx->active);
        if (err_msg) {
            StrBuf eb;
            sb_init(&eb);
            sb_append(&eb, "cannot read import file: ");
            sb_append(&eb, abs_path);
            *err_msg = sb_steal(&eb);
        }
        free(abs_path);
        return NULL;
    }

    sb_init(&import_buf);
    sb_init(&package_buf);
    sb_init(&body_buf);

    cursor = source;
    if ((unsigned char)cursor[0] == 0xEF && (unsigned char)cursor[1] == 0xBB && (unsigned char)cursor[2] == 0xBF) {
        cursor += 3;
    }
    while (*cursor) {
        const char *line_end = cursor;
        size_t line_len;
        int has_nl;
        char *line;
        const char *trimmed;
        ImportDirective d;

        while (*line_end && *line_end != '\n') line_end++;
        has_nl = (*line_end == '\n');
        line_len = (size_t)(line_end - cursor);
        line = dup_range(cursor, line_len);
        if (!line) {
            if (err_msg) *err_msg = dup_cstr("out of memory while parsing imports");
            goto fail;
        }

        trimmed = skip_spaces(line);
        if (starts_with_kw(trimmed, "package")) {
            if (is_entry && !package_taken) {
                if (!sb_append_n(&package_buf, line, line_len)) {
                    free(line);
                    if (err_msg) *err_msg = dup_cstr("out of memory while preserving package line");
                    goto fail;
                }
                if (has_nl && !sb_append_n(&package_buf, "\n", 1)) {
                    free(line);
                    if (err_msg) *err_msg = dup_cstr("out of memory while preserving package line");
                    goto fail;
                }
                package_taken = 1;
            }
            free(line);
            cursor = has_nl ? line_end + 1 : line_end;
            continue;
        }

        if (parse_import_directive(line, &d) && d.matched) {
            char *import_abs = resolve_import_path(abs_path, &d);
            char *imported_text;
            if (!import_abs) {
                import_directive_free(&d);
                free(line);
                if (err_msg) *err_msg = dup_cstr("cannot resolve import path");
                goto fail;
            }

            imported_text = resolve_imports_recursive(import_abs, ctx, err_msg);
            free(import_abs);
            if (!imported_text) {
                import_directive_free(&d);
                free(line);
                goto fail;
            }

            if (!sb_append(&import_buf, imported_text)) {
                free(imported_text);
                import_directive_free(&d);
                free(line);
                if (err_msg) *err_msg = dup_cstr("out of memory while merging import text");
                goto fail;
            }
            free(imported_text);

            if (d.rewrite_prefix && d.rewrite_prefix[0]) {
                if (!strlist_push_unique(&rewrite_prefixes, d.rewrite_prefix)) {
                    import_directive_free(&d);
                    free(line);
                    if (err_msg) *err_msg = dup_cstr("out of memory while storing import alias");
                    goto fail;
                }
            }

            import_directive_free(&d);
        } else {
            if (!sb_append_n(&body_buf, line, line_len)) {
                free(line);
                if (err_msg) *err_msg = dup_cstr("out of memory while building source body");
                goto fail;
            }
            if (has_nl && !sb_append_n(&body_buf, "\n", 1)) {
                free(line);
                if (err_msg) *err_msg = dup_cstr("out of memory while building source body");
                goto fail;
            }
        }

        free(line);
        cursor = has_nl ? line_end + 1 : line_end;
    }

    rewritten_body = rewrite_prefixed_calls(body_buf.data ? body_buf.data : "", &rewrite_prefixes);
    if (!rewritten_body) {
        if (err_msg) *err_msg = dup_cstr("out of memory while rewriting module prefixes");
        goto fail;
    }

    {
        StrBuf out;
        sb_init(&out);
        if (is_entry) {
            if (!sb_append(&out, package_buf.data ? package_buf.data : "")) {
                sb_free(&out);
                if (err_msg) *err_msg = dup_cstr("out of memory while composing final source");
                goto fail;
            }
        }
        if (!sb_append(&out, import_buf.data ? import_buf.data : "") ||
            !sb_append(&out, rewritten_body)) {
            sb_free(&out);
            if (err_msg) *err_msg = dup_cstr("out of memory while composing final source");
            goto fail;
        }
        final_text = sb_steal(&out);
    }

    if (!strlist_push_unique(&ctx->loaded, abs_path)) {
        if (err_msg) *err_msg = dup_cstr("out of memory while tracking loaded imports");
        goto fail;
    }

    strlist_pop(&ctx->active);
    strlist_free(&rewrite_prefixes);
    sb_free(&import_buf);
    sb_free(&package_buf);
    sb_free(&body_buf);
    free(rewritten_body);
    free(source);
    free(abs_path);
    return final_text;

fail:
    strlist_pop(&ctx->active);
    strlist_free(&rewrite_prefixes);
    sb_free(&import_buf);
    sb_free(&package_buf);
    sb_free(&body_buf);
    free(rewritten_body);
    free(final_text);
    free(source);
    free(abs_path);
    return NULL;
}

static char *resolve_source_with_imports(const char *entry_path) {
    ImportContext ctx;
    char *err = NULL;
    char *resolved;

    memset(&ctx, 0, sizeof(ctx));
    resolved = resolve_imports_recursive(entry_path, &ctx, &err);
    if (!resolved) {
        if (err) {
            fprintf(stderr, "Import error: %s\n", err);
            free(err);
        } else {
            fprintf(stderr, "Import error: unknown failure\n");
        }
    }

    strlist_free(&ctx.loaded);
    strlist_free(&ctx.active);
    return resolved;
}

static int write_c_file(const char *path, const char *code) {
    FILE *fp = fopen(path, "wb");
    if (!fp) return 0;
    fputs(code, fp);
    fclose(fp);
    return 1;
}

static int run_c_compiler(const char *c_path, const char *exe_path) {
    char command[1024];
    int written = snprintf(command, sizeof(command), "gcc -O2 -std=c99 \"%s\" -o \"%s\"", c_path, exe_path);
    if (written < 0 || written >= (int)sizeof(command)) return 0;
    return system(command) == 0;
}

static int write_bytecode(HVM_VM *vm, const char *path) {
    FILE *fp = fopen(path, "wb");
    if (!fp) return 0;
    {
        uint32_t count = (uint32_t)vm->instruction_count;
        size_t i;
        fwrite(MAGIC, 1, 4, fp);
        fwrite(&count, sizeof(uint32_t), 1, fp);
        for (i = 0; i < vm->instruction_count; i++) {
            HVM_Instruction *ins = &vm->instructions[i];
            uint8_t op = (uint8_t)ins->opcode;
            uint8_t kind = OP_NONE;
            fwrite(&op, 1, 1, fp);
            switch (ins->opcode) {
                case HVM_PUSH_INT:
                case HVM_PUSH_BOOL:
                case HVM_JUMP:
                case HVM_JUMP_IF_FALSE:
                case HVM_JUMP_IF_TRUE:
                case HVM_CALL:
                case HVM_STORE:
                case HVM_LOAD:
                    kind = OP_INT;
                    fwrite(&kind, 1, 1, fp);
                    fwrite(&ins->operand.int_operand, sizeof(int64_t), 1, fp);
                    break;
                case HVM_PUSH_FLOAT:
                    kind = OP_FLOAT;
                    fwrite(&kind, 1, 1, fp);
                    fwrite(&ins->operand.float_operand, sizeof(double), 1, fp);
                    break;
                case HVM_PUSH_STRING:
                case HVM_STORE_GLOBAL:
                case HVM_LOAD_GLOBAL:
                case HVM_CREATE_WINDOW:
                    kind = OP_STRING;
                    fwrite(&kind, 1, 1, fp);
                    if (ins->operand.string_operand) {
                        uint32_t len = (uint32_t)strlen(ins->operand.string_operand);
                        fwrite(&len, sizeof(uint32_t), 1, fp);
                        fwrite(ins->operand.string_operand, 1, len, fp);
                    } else {
                        uint32_t len = 0;
                        fwrite(&len, sizeof(uint32_t), 1, fp);
                    }
                    break;
                default:
                    kind = OP_NONE;
                    fwrite(&kind, 1, 1, fp);
                    break;
            }
        }
    }
    fclose(fp);
    return 1;
}

static HVM_Instruction *read_bytecode(const char *path, size_t *out_count) {
    FILE *fp = fopen(path, "rb");
    char magic[5] = {0};
    uint32_t count;
    HVM_Instruction *code;
    uint32_t i;

    if (!fp) return NULL;
    if (fread(magic, 1, 4, fp) != 4 || strncmp(magic, MAGIC, 4) != 0) { fclose(fp); return NULL; }
    if (fread(&count, sizeof(uint32_t), 1, fp) != 1) { fclose(fp); return NULL; }
    code = (HVM_Instruction *)calloc(count, sizeof(HVM_Instruction));
    if (!code) { fclose(fp); return NULL; }

    for (i = 0; i < count; i++) {
        uint8_t op, kind;
        if (fread(&op, 1, 1, fp) != 1) { free(code); fclose(fp); return NULL; }
        if (fread(&kind, 1, 1, fp) != 1) { free(code); fclose(fp); return NULL; }
        code[i].opcode = (HVM_Opcode)op;
        switch (kind) {
            case OP_INT:
                fread(&code[i].operand.int_operand, sizeof(int64_t), 1, fp);
                break;
            case OP_FLOAT:
                fread(&code[i].operand.float_operand, sizeof(double), 1, fp);
                break;
            case OP_STRING: {
                uint32_t len = 0;
                fread(&len, sizeof(uint32_t), 1, fp);
                code[i].operand.string_operand = (char *)calloc(len + 1, 1);
                if (len > 0 && code[i].operand.string_operand) {
                    fread(code[i].operand.string_operand, 1, len, fp);
                    code[i].operand.string_operand[len] = '\0';
                }
                break;
            }
            case OP_NONE:
            default:
                break;
        }
    }

    fclose(fp);
    *out_count = count;
    return code;
}

static int compile_to_bytecode(ASTNode *ast, const char *bc_path, int run_after) {
    int ok = 0;
    HVM_VM *vm = hvm_create();
    HVM_Compiler *c = hvm_compiler_create(vm);
    if (!vm || !c) goto done;
    if (!hvm_compiler_compile_ast(c, ast)) goto done;
    if (bc_path) {
        if (!write_bytecode(vm, bc_path)) goto done;
        printf("Generated bytecode: %s\n", bc_path);
    }
    if (run_after) {
        if (!hvm_run(vm)) {
            fprintf(stderr, "VM execution failed\n");
            goto done;
        }
    }
    ok = 1;

done:
    hvm_compiler_destroy(c);
    hvm_destroy(vm);
    return ok;
}

static void print_usage(const char *exe) {
    fprintf(stderr, "Usage:\n");
    fprintf(stderr, "  %s <input.hosc> [-c out.c] [-o out.exe] [-b out.hbc] [-r]\n", exe);
    fprintf(stderr, "Options:\n");
    fprintf(stderr, "  -c  emit C source\n");
    fprintf(stderr, "  -o  build native exe via gcc (needs gcc)\n");
    fprintf(stderr, "  -b  emit bytecode file (.hbc)\n");
    fprintf(stderr, "  -r  run with VM (no gcc needed)\n");
}

int main(int argc, char **argv) {
    const char *input_path;
    const char *c_output_path = NULL;
    const char *exe_output_path = NULL;
    const char *bc_output_path = NULL;
    int run_vm = 0;
    int i;
    char *source;
    ASTNode *ast;

    (void)read_bytecode;

    if (argc < 2) { print_usage(argv[0]); return 1; }
    input_path = argv[1];

    for (i = 2; i < argc; i++) {
        if (strcmp(argv[i], "-c") == 0 && i + 1 < argc) {
            c_output_path = argv[++i];
        } else if (strcmp(argv[i], "-o") == 0 && i + 1 < argc) {
            exe_output_path = argv[++i];
        } else if (strcmp(argv[i], "-b") == 0 && i + 1 < argc) {
            bc_output_path = argv[++i];
        } else if (strcmp(argv[i], "-r") == 0) {
            run_vm = 1;
        } else {
            print_usage(argv[0]);
            return 1;
        }
    }

    source = resolve_source_with_imports(input_path);
    if (!source) {
        fprintf(stderr, "Error: import resolution failed for %s\n", input_path);
        return 1;
    }

    ast = parser_parse(source);
    free(source);
    if (!ast) {
        fprintf(stderr, "Error: parse failed\n");
        return 1;
    }

    if (bc_output_path || run_vm) {
        int ok = compile_to_bytecode(ast, bc_output_path, run_vm);
        free_ast(ast);
        ast_release_arena();
        return ok ? 0 : 1;
    }

    if (!c_output_path) c_output_path = "output.c";

    {
        char *code = codegen_generate(ast);
        free_ast(ast);
        ast_release_arena();
        if (!code) {
            fprintf(stderr, "Error: codegen failed\n");
            return 1;
        }

        if (!write_c_file(c_output_path, code)) {
            free(code);
            fprintf(stderr, "Error: cannot write %s\n", c_output_path);
            return 1;
        }
        printf("Generated C file: %s\n", c_output_path);

        if (exe_output_path) {
            if (!run_c_compiler(c_output_path, exe_output_path)) {
                free(code);
                fprintf(stderr, "Error: native compilation failed\n");
                return 1;
            }
            printf("Generated executable: %s\n", exe_output_path);
        }

        free(code);
    }

    return 0;
}







