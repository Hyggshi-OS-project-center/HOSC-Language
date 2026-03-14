/*
 * File: runtime\src\hvm_memory.c
 * Purpose: HOSC source file.
 */

static void hvm_free_value(HVM_Value* v) {
    if (!v) return;
    if (v->type == HVM_TYPE_STRING) {
        v->data.string_value = NULL;
    }
    v->type = HVM_TYPE_NULL;
}

static void hvm_set_error_msg(HVM_VM* vm, const char* msg) {
    if (!vm) return;
    if (vm->error_message) free(vm->error_message);
    vm->error_message = msg ? strdup(msg) : NULL;
}

static int hvm_is_truthy(HVM_Value v) {
    switch (v.type) {
        case HVM_TYPE_BOOL: return v.data.bool_value != 0;
        case HVM_TYPE_INT: return v.data.int_value != 0;
        case HVM_TYPE_FLOAT: return v.data.float_value != 0.0;
        case HVM_TYPE_STRING: return v.data.string_value && v.data.string_value[0] != '\0';
        default: return 0;
    }
}

static int hvm_is_numeric(HVM_Value v) {
    return v.type == HVM_TYPE_INT || v.type == HVM_TYPE_FLOAT || v.type == HVM_TYPE_BOOL;
}

static double hvm_to_double(HVM_Value v) {
    if (v.type == HVM_TYPE_FLOAT) return v.data.float_value;
    if (v.type == HVM_TYPE_BOOL) return v.data.bool_value ? 1.0 : 0.0;
    return (double)v.data.int_value;
}

static int hvm_ensure_buffer(char **buffer, size_t *cap, size_t needed) {
    size_t new_cap;
    char *nb;
    if (!buffer || !cap) return 0;
    if (*cap >= needed) return 1;
    new_cap = (*cap == 0) ? 64 : *cap;
    while (new_cap < needed) {
        new_cap *= 2;
    }
    nb = (char *)realloc(*buffer, new_cap);
    if (!nb) return 0;
    *buffer = nb;
    *cap = new_cap;
    return 1;
}

static const char *hvm_value_to_cstring(HVM_VM *vm, HVM_Value v, char **buffer, size_t *cap) {
    char tmp[128];
    const char *src = NULL;
    size_t len;
    (void)vm;
    if (v.type == HVM_TYPE_STRING) {
        return v.data.string_value ? v.data.string_value : "";
    }
    if (v.type == HVM_TYPE_FLOAT) {
        snprintf(tmp, sizeof(tmp), "%g", v.data.float_value);
        src = tmp;
    } else if (v.type == HVM_TYPE_BOOL) {
        src = v.data.bool_value ? "true" : "false";
    } else if (v.type == HVM_TYPE_INT) {
        snprintf(tmp, sizeof(tmp), "%lld", (long long)v.data.int_value);
        src = tmp;
    } else {
        src = "null";
    }

    len = strlen(src);
    if (!hvm_ensure_buffer(buffer, cap, len + 1)) return "";
    memcpy(*buffer, src, len + 1);
    return *buffer;
}

static char* hvm_value_to_string(HVM_Value v) {
    char buf[128];
    if (v.type == HVM_TYPE_STRING) return strdup(v.data.string_value ? v.data.string_value : "");
    if (v.type == HVM_TYPE_FLOAT) {
        snprintf(buf, sizeof(buf), "%g", v.data.float_value);
        return strdup(buf);
    }
    if (v.type == HVM_TYPE_BOOL) return strdup(v.data.bool_value ? "true" : "false");
    if (v.type == HVM_TYPE_INT) {
        snprintf(buf, sizeof(buf), "%lld", (long long)v.data.int_value);
        return strdup(buf);
    }
    return strdup("null");
}

static char *hvm_read_text_file(const char *path) {
    FILE *fp;
    long size;
    size_t read_size;
    char *buffer;

    if (!path || path[0] == '\0') return strdup("");

    fp = fopen(path, "rb");
    if (!fp) return strdup("");

    if (fseek(fp, 0, SEEK_END) != 0) {
        fclose(fp);
        return strdup("");
    }

    size = ftell(fp);
    if (size < 0) {
        fclose(fp);
        return strdup("");
    }

    if (fseek(fp, 0, SEEK_SET) != 0) {
        fclose(fp);
        return strdup("");
    }

    buffer = (char *)malloc((size_t)size + 1);
    if (!buffer) {
        fclose(fp);
        return strdup("");
    }

    read_size = fread(buffer, 1, (size_t)size, fp);
    buffer[read_size] = '\0';
    fclose(fp);
    return buffer;
}

static char *hvm_read_text_line(const char *path, int line_no) {
    char *content;
    char *start;
    char *p;
    char *line;
    size_t len;
    int current_line = 1;

    if (line_no < 1) line_no = 1;

    content = hvm_read_text_file(path);
    if (!content) return strdup("");

    start = content;
    p = content;
    while (*p && current_line < line_no) {
        if (*p == '\n') {
            current_line++;
            start = p + 1;
        }
        p++;
    }

    if (current_line != line_no) {
        free(content);
        return strdup("");
    }

    p = start;
    while (*p && *p != '\n' && *p != '\r') p++;

    len = (size_t)(p - start);
    line = (char *)malloc(len + 1);
    if (!line) {
        free(content);
        return strdup("");
    }

    memcpy(line, start, len);
    line[len] = '\0';
    free(content);
    return line;
}

static int hvm_write_text_file(const char *path, const char *text) {
    FILE *fp;
    size_t len;
    size_t written;

    if (!path || path[0] == '\0') return 0;

    fp = fopen(path, "wb");
    if (!fp) return 0;

    if (!text) text = "";
    len = strlen(text);
    written = fwrite(text, 1, len, fp);

    if (fclose(fp) != 0) return 0;
    return written == len;
}

static char *hvm_exec_shell_command(const char *cmd) {
    FILE *pipe;
    char chunk[256];
    char *output;
    size_t len = 0;
    size_t cap = 1;

    if (!cmd || cmd[0] == '\0') return strdup("");

#ifdef _WIN32
    pipe = _popen(cmd, "r");
#else
    pipe = popen(cmd, "r");
#endif
    if (!pipe) return strdup("");

    output = (char *)malloc(cap);
    if (!output) {
#ifdef _WIN32
        _pclose(pipe);
#else
        pclose(pipe);
#endif
        return strdup("");
    }
    output[0] = '\0';

    while (fgets(chunk, (int)sizeof(chunk), pipe)) {
        size_t c_len = strlen(chunk);
        if (len + c_len + 1 > cap) {
            size_t new_cap = cap;
            char *n;
            while (len + c_len + 1 > new_cap) {
                new_cap *= 2;
            }
            n = (char *)realloc(output, new_cap);
            if (!n) {
                free(output);
#ifdef _WIN32
                _pclose(pipe);
#else
                pclose(pipe);
#endif
                return strdup("");
            }
            output = n;
            cap = new_cap;
        }
        memcpy(output + len, chunk, c_len + 1);
        len += c_len;
    }

#ifdef _WIN32
    _pclose(pipe);
#else
    pclose(pipe);
#endif
    return output;
}

static int find_global_index(HVM_VM* vm, const char* name) {
    size_t i;
    for (i = 0; i < vm->string_count; i++) {
        if (vm->strings[i] && strcmp(vm->strings[i], name) == 0) return (int)i;
    }
    return -1;
}

static char *resolve_runtime_name(HVM_VM* vm, const char* name) {
    int needed;
    char *out;
    if (!vm || !name) return NULL;
    if (strncmp(name, "__", 2) != 0 || vm->call_top == 0) return NULL;
    needed = snprintf(NULL, 0, "%s#%zu", name, vm->call_top);
    if (needed < 0) return NULL;
    out = (char *)malloc((size_t)needed + 1);
    if (!out) return NULL;
    snprintf(out, (size_t)needed + 1, "%s#%zu", name, vm->call_top);
    return out;
}

static int store_global(HVM_VM* vm, const char* name, HVM_Value value) {
    int idx;
    char *resolved_name;
    const char *key;

    if (!vm || !name) return 0;
    resolved_name = resolve_runtime_name(vm, name);
    key = resolved_name ? resolved_name : name;

    idx = find_global_index(vm, key);
    if (idx >= 0) {
        hvm_free_value(&vm->memory[idx]);
        vm->memory[idx] = value;
        free(resolved_name);
        return 1;
    }

    if (vm->string_count >= HVM_MAX_GLOBALS || vm->memory_used >= HVM_MEMORY_SIZE) {
        free(resolved_name);
        return 0;
    }

    vm->strings[vm->string_count] = strdup(key);
    if (!vm->strings[vm->string_count]) {
        free(resolved_name);
        return 0;
    }

    vm->memory[vm->string_count] = value;
    vm->string_count++;
    vm->memory_used++;
    free(resolved_name);
    return 1;
}

static int load_global(HVM_VM* vm, const char* name, HVM_Value* out) {
    int idx;
    char *resolved_name;
    const char *key;

    if (!vm || !name || !out) return 0;
    resolved_name = resolve_runtime_name(vm, name);
    key = resolved_name ? resolved_name : name;

    idx = find_global_index(vm, key);
    free(resolved_name);
    if (idx < 0) return 0;

    *out = vm->memory[idx];
    return 1;
}
