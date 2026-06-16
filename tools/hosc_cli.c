/*
 * File: tools/hosc_cli.c
 * Purpose: HOSC compiler CLI entry point.
 */

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "hosc_compiler.h"

#define HOSC_VERSION "0.1.3"
#define HOSC_FMT_TAB_WIDTH 4

static int streq(const char *a, const char *b) {
    return strcmp(a, b) == 0;
}

static int ends_with_ci(const char *value, const char *suffix) {
    size_t value_len;
    size_t suffix_len;
    size_t i;
    if (!value || !suffix) return 0;
    value_len = strlen(value);
    suffix_len = strlen(suffix);
    if (suffix_len > value_len) return 0;
    for (i = 0; i < suffix_len; i++) {
        unsigned char lhs = (unsigned char)value[value_len - suffix_len + i];
        unsigned char rhs = (unsigned char)suffix[i];
        if (tolower(lhs) != tolower(rhs)) return 0;
    }
    return 1;
}

static char *replace_extension(const char *path, const char *ext) {
    const char *dot;
    const char *slash1;
    const char *slash2;
    const char *slash;
    size_t base_len;
    size_t ext_len;
    char *out;
    if (!path || !ext) return NULL;
    dot = strrchr(path, '.');
    slash1 = strrchr(path, '/');
    slash2 = strrchr(path, '\\');
    slash = slash1;
    if (!slash || (slash2 && slash2 > slash)) slash = slash2;
    if (dot && (!slash || dot > slash)) base_len = (size_t)(dot - path);
    else base_len = strlen(path);
    ext_len = strlen(ext);
    out = (char *)malloc(base_len + ext_len + 1);
    if (!out) return NULL;
    memcpy(out, path, base_len);
    memcpy(out + base_len, ext, ext_len + 1);
    return out;
}

static char *make_temp_path(const char *ext) {
    char tmp[L_tmpnam];
    size_t len;
    char *out;
    if (!ext) return NULL;
    if (!tmpnam(tmp)) return NULL;
    len = strlen(tmp) + strlen(ext);
    out = (char *)malloc(len + 1);
    if (!out) return NULL;
    snprintf(out, len + 1, "%s%s", tmp, ext);
    return out;
}

static int read_text_file(const char *path, char **out_buf, size_t *out_len) {
    FILE *fp;
    long size;
    char *buf;
    size_t read_count;
    if (!path || !out_buf || !out_len) return 0;
    fp = fopen(path, "rb");
    if (!fp) return 0;
    if (fseek(fp, 0, SEEK_END) != 0) {
        fclose(fp);
        return 0;
    }
    size = ftell(fp);
    if (size < 0) {
        fclose(fp);
        return 0;
    }
    if (fseek(fp, 0, SEEK_SET) != 0) {
        fclose(fp);
        return 0;
    }
    buf = (char *)malloc((size_t)size + 1);
    if (!buf) {
        fclose(fp);
        return 0;
    }
    read_count = fread(buf, 1, (size_t)size, fp);
    fclose(fp);
    if (read_count != (size_t)size) {
        free(buf);
        return 0;
    }
    buf[read_count] = '\0';
    *out_buf = buf;
    *out_len = read_count;
    return 1;
}

static int write_text_file(const char *path, const char *buf, size_t len) {
    FILE *fp;
    if (!path || !buf) return 0;
    fp = fopen(path, "wb");
    if (!fp) return 0;
    if (len > 0 && fwrite(buf, 1, len, fp) != len) {
        fclose(fp);
        return 0;
    }
    fclose(fp);
    return 1;
}

static int ensure_capacity(char **buf, size_t *cap, size_t needed) {
    size_t new_cap;
    char *new_buf;
    if (needed <= *cap) return 1;
    new_cap = (*cap == 0) ? 256 : *cap;
    while (new_cap < needed) new_cap *= 2;
    new_buf = (char *)realloc(*buf, new_cap);
    if (!new_buf) return 0;
    *buf = new_buf;
    *cap = new_cap;
    return 1;
}

static char *format_source_text(const char *input, size_t len, size_t *out_len) {
    char *out = NULL;
    size_t cap = 0;
    size_t o = 0;
    size_t line_start = 0;
    size_t i;

    if (!ensure_capacity(&out, &cap, len + 8)) return NULL;

    for (i = 0; i < len; i++) {
        unsigned char ch = (unsigned char)input[i];
        if (ch == '\r' || ch == '\n') {
            while (o > line_start && out[o - 1] == ' ') o--;
            if (!ensure_capacity(&out, &cap, o + 2)) {
                free(out);
                return NULL;
            }
            out[o++] = '\n';
            if (ch == '\r' && i + 1 < len && input[i + 1] == '\n') i++;
            line_start = o;
            continue;
        }
        if (ch == '\t') {
            size_t j;
            if (!ensure_capacity(&out, &cap, o + HOSC_FMT_TAB_WIDTH + 1)) {
                free(out);
                return NULL;
            }
            for (j = 0; j < HOSC_FMT_TAB_WIDTH; j++) out[o++] = ' ';
            continue;
        }
        if (!ensure_capacity(&out, &cap, o + 2)) {
            free(out);
            return NULL;
        }
        out[o++] = (char)ch;
    }

    while (o > line_start && out[o - 1] == ' ') o--;
    if (o == 0 || out[o - 1] != '\n') {
        if (!ensure_capacity(&out, &cap, o + 2)) {
            free(out);
            return NULL;
        }
        out[o++] = '\n';
    }
    out[o] = '\0';
    if (out_len) *out_len = o;
    return out;
}

static int run_legacy(int argc, char **argv) {
    return hosc_compile_cli(argc, argv);
}

static int run_check_command(const char *prog, const char *input_path) {
    char *tmp_hbc = make_temp_path(".hbc");
    char *argv_local[4];
    int rc;
    if (!tmp_hbc) {
        fprintf(stderr, "Error: unable to create temporary file for check\n");
        return 1;
    }
    argv_local[0] = (char *)prog;
    argv_local[1] = (char *)input_path;
    argv_local[2] = "-b";
    argv_local[3] = tmp_hbc;
    rc = hosc_compile_cli(4, argv_local);
    remove(tmp_hbc);
    free(tmp_hbc);
    return rc;
}

static int run_build_command(const char *prog, const char *input_path, const char *output_path) {
    char *default_exe = NULL;
    char *temp_c = NULL;
    char *argv_local[6];
    const char *target_path = output_path;
    int rc;

    if (target_path && ends_with_ci(target_path, ".hbc")) {
        argv_local[0] = (char *)prog;
        argv_local[1] = (char *)input_path;
        argv_local[2] = "-b";
        argv_local[3] = (char *)target_path;
        return hosc_compile_cli(4, argv_local);
    }

    if (!target_path) {
        default_exe = replace_extension(input_path, ".exe");
        if (!default_exe) {
            fprintf(stderr, "Error: unable to determine default output path\n");
            return 1;
        }
        target_path = default_exe;
    }

    temp_c = make_temp_path(".c");
    if (!temp_c) {
        free(default_exe);
        fprintf(stderr, "Error: unable to create temporary C file\n");
        return 1;
    }

    argv_local[0] = (char *)prog;
    argv_local[1] = (char *)input_path;
    argv_local[2] = "-c";
    argv_local[3] = temp_c;
    argv_local[4] = "-o";
    argv_local[5] = (char *)target_path;

    rc = hosc_compile_cli(6, argv_local);
    remove(temp_c);
    free(temp_c);
    free(default_exe);
    return rc;
}

static int run_run_command(const char *prog, const char *input_path, const char *output_path, int keep_temp) {
    char *temp_hbc = NULL;
    char *argv_local[5];
    const char *target_path = output_path;
    int rc;

    if (!target_path) {
        temp_hbc = make_temp_path(".hbc");
        if (!temp_hbc) {
            fprintf(stderr, "Error: unable to create temporary bytecode file\n");
            return 1;
        }
        target_path = temp_hbc;
    }

    argv_local[0] = (char *)prog;
    argv_local[1] = (char *)input_path;
    argv_local[2] = "-b";
    argv_local[3] = (char *)target_path;
    argv_local[4] = "-r";

    rc = hosc_compile_cli(5, argv_local);
    if (temp_hbc && !keep_temp) remove(temp_hbc);
    free(temp_hbc);
    return rc;
}

static int run_fmt_command(const char *input_path, const char *output_path, int check_only) {
    char *source = NULL;
    char *formatted = NULL;
    size_t source_len = 0;
    size_t formatted_len = 0;
    const char *dest_path = output_path ? output_path : input_path;
    int changed;
    int ok;

    if (!read_text_file(input_path, &source, &source_len)) {
        fprintf(stderr, "Error: cannot read %s\n", input_path);
        return 1;
    }

    formatted = format_source_text(source, source_len, &formatted_len);
    if (!formatted) {
        free(source);
        fprintf(stderr, "Error: format failed\n");
        return 1;
    }

    changed = (source_len != formatted_len) || (memcmp(source, formatted, source_len) != 0);
    if (check_only) {
        free(source);
        free(formatted);
        if (changed) {
            fprintf(stderr, "Formatting required: %s\n", input_path);
            return 1;
        }
        return 0;
    }

    ok = write_text_file(dest_path, formatted, formatted_len);
    free(source);
    free(formatted);
    if (!ok) {
        fprintf(stderr, "Error: cannot write %s\n", dest_path);
        return 1;
    }
    printf("Formatted: %s\n", dest_path);
    return 0;
}

static int print_version(void) {
    printf("hosc %s\n", HOSC_VERSION);
    return 0;
}

static int print_modern_usage(const char *prog) {
    fprintf(stderr, "Usage:\n");
    fprintf(stderr, "  %s --version\n", prog);
    fprintf(stderr, "  %s version\n", prog);
    fprintf(stderr, "  %s check <input.hosc>\n", prog);
    fprintf(stderr, "  %s build <input.hosc> [-o output.hbc|output.exe]\n", prog);
    fprintf(stderr, "  %s run <input.hosc> [-o output.hbc] [--keep]\n", prog);
    fprintf(stderr, "  %s fmt <input.hosc> [-o output.hosc] [--check]\n", prog);
    fprintf(stderr, "  %s <input.hosc> [-c out.c] [-o out.exe] [-b out.hbc] [-r]\n", prog);
    return 1;
}

int main(int argc, char **argv) {
    const char *cmd;

    if (argc < 2) return print_modern_usage(argv[0]);

    cmd = argv[1];
    if (streq(cmd, "--version") || streq(cmd, "version")) {
        return print_version();
    }

    if (streq(cmd, "check")) {
        if (argc != 3) return print_modern_usage(argv[0]);
        return run_check_command(argv[0], argv[2]);
    }

    if (streq(cmd, "build")) {
        const char *output_path = NULL;
        int i;
        if (argc < 3) return print_modern_usage(argv[0]);
        for (i = 3; i < argc; i++) {
            if (streq(argv[i], "-o") && i + 1 < argc) {
                output_path = argv[++i];
            } else {
                return print_modern_usage(argv[0]);
            }
        }
        return run_build_command(argv[0], argv[2], output_path);
    }

    if (streq(cmd, "run")) {
        const char *output_path = NULL;
        int keep_temp = 0;
        int i;
        if (argc < 3) return print_modern_usage(argv[0]);
        for (i = 3; i < argc; i++) {
            if (streq(argv[i], "-o") && i + 1 < argc) {
                output_path = argv[++i];
            } else if (streq(argv[i], "--keep")) {
                keep_temp = 1;
            } else {
                return print_modern_usage(argv[0]);
            }
        }
        return run_run_command(argv[0], argv[2], output_path, keep_temp);
    }

    if (streq(cmd, "fmt")) {
        const char *output_path = NULL;
        int check_only = 0;
        int i;
        if (argc < 3) return print_modern_usage(argv[0]);
        for (i = 3; i < argc; i++) {
            if (streq(argv[i], "-o") && i + 1 < argc) {
                output_path = argv[++i];
            } else if (streq(argv[i], "--check")) {
                check_only = 1;
            } else {
                return print_modern_usage(argv[0]);
            }
        }
        return run_fmt_command(argv[2], output_path, check_only);
    }

    return run_legacy(argc, argv);
}

