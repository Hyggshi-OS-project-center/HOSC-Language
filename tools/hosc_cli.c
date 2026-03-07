#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <ctype.h>

#ifdef _WIN32
#include <windows.h>
#endif

#ifndef _WIN32
#error "hosc CLI currently targets Windows"
#endif

#ifndef MAX_PATH
#define MAX_PATH 260
#endif

#define HOSC_CLI_VERSION "0.3.0"
#define HOSC_BUNDLE_MAGIC "HOSCEXE1"
#define HOSC_BUNDLE_MAGIC_LEN 8

static void print_usage(void) {
    printf("HOSC CLI\n");
    printf("Usage:\n");
    printf("  hosc --version\n");
    printf("  hosc build <input.hosc> [-o output.exe|output.hbc]\n");
    printf("  hosc run   <input.hosc> [-o output.hbc] [--keep]\n");
    printf("  hosc check <input.hosc>\n");
    printf("  hosc fmt   <input.hosc> [-o output.hosc] [--check]\n");
    printf("  hosc version\n");
    printf("\n");
    printf("Notes:\n");
    printf("  build default output: <input>.exe (bundled runtime + bytecode)\n");
    printf("  use -o <file.hbc> to emit raw bytecode\n");
}

static int file_exists(const char *path) {
    DWORD attrs;
    if (!path || !path[0]) return 0;
    attrs = GetFileAttributesA(path);
    return attrs != INVALID_FILE_ATTRIBUTES && !(attrs & FILE_ATTRIBUTE_DIRECTORY);
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

static void get_exe_dir(char *out, size_t out_size) {
    char module_path[MAX_PATH];
    DWORD len;

    if (!out || out_size == 0) return;
    out[0] = '\0';

    len = GetModuleFileNameA(NULL, module_path, (DWORD)sizeof(module_path));
    if (len == 0 || len >= sizeof(module_path)) return;

    module_path[len] = '\0';
    get_dirname(module_path, out, out_size);
}

static void path_join(const char *dir, const char *file, char *out, size_t out_size) {
    if (!out || out_size == 0) return;
    if (!dir || !dir[0]) {
        snprintf(out, out_size, "%s", file ? file : "");
        return;
    }

    if (!file) file = "";
    snprintf(out, out_size, "%s\\%s", dir, file);
}

static void default_exe_output_path(const char *input, char *out, size_t out_size) {
    const char *dot;
    const char *s1;
    const char *s2;
    const char *slash;
    size_t base_len;

    if (!out || out_size == 0) return;
    out[0] = '\0';
    if (!input) return;

    s1 = strrchr(input, '\\');
    s2 = strrchr(input, '/');
    slash = (s1 && s2) ? ((s1 > s2) ? s1 : s2) : (s1 ? s1 : s2);
    dot = strrchr(input, '.');

    if (dot && (!slash || dot > slash)) {
        base_len = (size_t)(dot - input);
    } else {
        base_len = strlen(input);
    }

    if (base_len + 4 >= out_size) {
        base_len = out_size - 5;
    }

    memcpy(out, input, base_len);
    memcpy(out + base_len, ".exe", 5);
}

static int make_temp_output(char *out, size_t out_size) {
    char temp_dir[MAX_PATH];
    char temp_file[MAX_PATH];
    UINT len;

    if (!out || out_size == 0) return 0;
    out[0] = '\0';

    len = GetTempPathA((DWORD)sizeof(temp_dir), temp_dir);
    if (len == 0 || len >= sizeof(temp_dir)) return 0;

    if (!GetTempFileNameA(temp_dir, "hbc", 0, temp_file)) return 0;

    if (snprintf(out, out_size, "%s.hbc", temp_file) >= (int)out_size) return 0;
    DeleteFileA(temp_file);
    return 1;
}

static int run_command(const char *cmd) {
    STARTUPINFOA si;
    PROCESS_INFORMATION pi;
    char mutable_cmd[4096];
    size_t len;
    DWORD exit_code = 1;

    if (!cmd) return 1;

    len = strlen(cmd);
    if (len >= sizeof(mutable_cmd)) {
        fprintf(stderr, "Command too long\n");
        return 1;
    }

    memcpy(mutable_cmd, cmd, len + 1);

    if (getenv("HOSC_DEBUG")) {
        fprintf(stderr, "[hosc] %s\n", mutable_cmd);
    }

    ZeroMemory(&si, sizeof(si));
    ZeroMemory(&pi, sizeof(pi));
    si.cb = sizeof(si);

    if (!CreateProcessA(NULL, mutable_cmd, NULL, NULL, FALSE, 0, NULL, NULL, &si, &pi)) {
        fprintf(stderr, "Failed to launch process (error %lu)\n", GetLastError());
        return 1;
    }

    WaitForSingleObject(pi.hProcess, INFINITE);
    if (!GetExitCodeProcess(pi.hProcess, &exit_code)) {
        exit_code = 1;
    }

    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);
    return (int)exit_code;
}

static int read_file_all(const char *path, char **data_out, size_t *len_out) {
    FILE *f;
    long file_size;
    size_t read_count;
    char *buffer;

    if (!path || !data_out || !len_out) return 1;
    *data_out = NULL;
    *len_out = 0;

    f = fopen(path, "rb");
    if (!f) {
        fprintf(stderr, "Failed to open file: %s\n", path);
        return 1;
    }

    if (fseek(f, 0, SEEK_END) != 0) {
        fclose(f);
        fprintf(stderr, "Failed to seek file: %s\n", path);
        return 1;
    }

    file_size = ftell(f);
    if (file_size < 0) {
        fclose(f);
        fprintf(stderr, "Failed to read file size: %s\n", path);
        return 1;
    }

    if (fseek(f, 0, SEEK_SET) != 0) {
        fclose(f);
        fprintf(stderr, "Failed to seek file: %s\n", path);
        return 1;
    }

    buffer = (char *)malloc((size_t)file_size + 1);
    if (!buffer) {
        fclose(f);
        fprintf(stderr, "Out of memory\n");
        return 1;
    }

    read_count = fread(buffer, 1, (size_t)file_size, f);
    fclose(f);

    if (read_count != (size_t)file_size) {
        free(buffer);
        fprintf(stderr, "Failed to read file: %s\n", path);
        return 1;
    }

    buffer[read_count] = '\0';
    *data_out = buffer;
    *len_out = read_count;
    return 0;
}

static int write_file_all(const char *path, const char *data, size_t len) {
    FILE *f;
    size_t write_count;

    if (!path || !data) return 1;

    f = fopen(path, "wb");
    if (!f) {
        fprintf(stderr, "Failed to write file: %s\n", path);
        return 1;
    }

    write_count = fwrite(data, 1, len, f);
    fclose(f);

    if (write_count != len) {
        fprintf(stderr, "Failed to write full file: %s\n", path);
        return 1;
    }

    return 0;
}

static int path_has_ext_ci(const char *path, const char *ext) {
    size_t path_len;
    size_t ext_len;
    size_t i;

    if (!path || !ext) return 0;
    path_len = strlen(path);
    ext_len = strlen(ext);
    if (path_len < ext_len) return 0;

    for (i = 0; i < ext_len; i++) {
        unsigned char a = (unsigned char)path[path_len - ext_len + i];
        unsigned char b = (unsigned char)ext[i];
        if (tolower(a) != tolower(b)) return 0;
    }

    return 1;
}

static int bundle_runtime_executable(const char *runtime_path, const char *bytecode_path, const char *output_path) {
    char *runtime_data = NULL;
    char *bytecode_data = NULL;
    char *bundle_data = NULL;
    size_t runtime_len = 0;
    size_t bytecode_len = 0;
    size_t total_len;
    uint64_t bytecode_size_u64;
    int rc = 1;

    if (read_file_all(runtime_path, &runtime_data, &runtime_len) != 0) {
        fprintf(stderr, "Failed to read runtime template: %s\n", runtime_path);
        goto cleanup;
    }

    if (read_file_all(bytecode_path, &bytecode_data, &bytecode_len) != 0) {
        fprintf(stderr, "Failed to read bytecode: %s\n", bytecode_path);
        goto cleanup;
    }

    if (runtime_len > (SIZE_MAX - bytecode_len - HOSC_BUNDLE_MAGIC_LEN - sizeof(uint64_t))) {
        fprintf(stderr, "Bundle output too large\n");
        goto cleanup;
    }

    total_len = runtime_len + bytecode_len + HOSC_BUNDLE_MAGIC_LEN + sizeof(uint64_t);
    bundle_data = (char *)malloc(total_len);
    if (!bundle_data) {
        fprintf(stderr, "Out of memory while creating bundle\n");
        goto cleanup;
    }

    memcpy(bundle_data, runtime_data, runtime_len);
    memcpy(bundle_data + runtime_len, bytecode_data, bytecode_len);
    memcpy(bundle_data + runtime_len + bytecode_len, HOSC_BUNDLE_MAGIC, HOSC_BUNDLE_MAGIC_LEN);

    bytecode_size_u64 = (uint64_t)bytecode_len;
    memcpy(bundle_data + runtime_len + bytecode_len + HOSC_BUNDLE_MAGIC_LEN, &bytecode_size_u64, sizeof(uint64_t));

    if (write_file_all(output_path, bundle_data, total_len) != 0) {
        goto cleanup;
    }

    rc = 0;

cleanup:
    free(runtime_data);
    free(bytecode_data);
    free(bundle_data);
    return rc;
}

static char *format_hosc_source(const char *input, size_t len, size_t *out_len, int *changed_out) {
    size_t cap;
    char *out;
    size_t i;
    size_t o;
    size_t line_start;

    if (!input || !out_len || !changed_out) return NULL;

    cap = (len * 4) + 4;
    out = (char *)malloc(cap);
    if (!out) return NULL;

    i = 0;
    o = 0;
    line_start = 0;

    while (i < len) {
        unsigned char c = (unsigned char)input[i++];

        if (c == '\r') {
            if (i < len && input[i] == '\n') {
                i++;
            }
            while (o > line_start && (out[o - 1] == ' ' || out[o - 1] == '\t')) {
                o--;
            }
            out[o++] = '\n';
            line_start = o;
            continue;
        }

        if (c == '\n') {
            while (o > line_start && (out[o - 1] == ' ' || out[o - 1] == '\t')) {
                o--;
            }
            out[o++] = '\n';
            line_start = o;
            continue;
        }

        if (c == '\t') {
            out[o++] = ' ';
            out[o++] = ' ';
            out[o++] = ' ';
            out[o++] = ' ';
            continue;
        }

        out[o++] = (char)c;
    }

    while (o > line_start && (out[o - 1] == ' ' || out[o - 1] == '\t')) {
        o--;
    }

    if (o == 0 || out[o - 1] != '\n') {
        out[o++] = '\n';
    }

    out[o] = '\0';
    *out_len = o;

    if (o != len || memcmp(input, out, len) != 0) {
        *changed_out = 1;
    } else {
        *changed_out = 0;
    }

    return out;
}

static int handle_fmt_command(int argc, char **argv) {
    const char *input;
    const char *out_arg = NULL;
    int check_only = 0;
    int i;

    char *source = NULL;
    size_t source_len = 0;

    char *formatted = NULL;
    size_t formatted_len = 0;
    int changed = 0;

    const char *write_path;
    int rc = 1;

    if (argc < 3) {
        fprintf(stderr, "fmt requires an input file\n");
        return 1;
    }

    input = argv[2];

    for (i = 3; i < argc; i++) {
        if (strcmp(argv[i], "-o") == 0) {
            if (i + 1 >= argc) {
                fprintf(stderr, "Missing value for -o\n");
                return 1;
            }
            out_arg = argv[++i];
        } else if (strcmp(argv[i], "--check") == 0) {
            check_only = 1;
        } else {
            fprintf(stderr, "Unknown option for fmt: %s\n", argv[i]);
            return 1;
        }
    }

    if (check_only && out_arg) {
        fprintf(stderr, "fmt --check cannot be combined with -o\n");
        return 1;
    }

    if (read_file_all(input, &source, &source_len) != 0) {
        return 1;
    }

    formatted = format_hosc_source(source, source_len, &formatted_len, &changed);
    if (!formatted) {
        fprintf(stderr, "Out of memory\n");
        goto cleanup;
    }

    if (check_only) {
        if (changed) {
            fprintf(stderr, "Needs formatting: %s\n", input);
            rc = 1;
        } else {
            printf("Fmt OK: %s\n", input);
            rc = 0;
        }
        goto cleanup;
    }

    write_path = out_arg ? out_arg : input;

    if (!changed && !out_arg) {
        printf("Fmt OK (no changes): %s\n", input);
        rc = 0;
        goto cleanup;
    }

    if (write_file_all(write_path, formatted, formatted_len) != 0) {
        rc = 1;
        goto cleanup;
    }

    printf("Formatted: %s\n", write_path);
    rc = 0;

cleanup:
    free(source);
    free(formatted);
    return rc;
}

int main(int argc, char **argv) {
    const char *command;
    const char *input;
    const char *out_arg = NULL;
    int keep_output = 0;
    int i;

    char exe_dir[MAX_PATH];
    char compiler_path[MAX_PATH];
    char vm_path[MAX_PATH];

    char output_path[MAX_PATH * 2];
    char temp_path[MAX_PATH * 2];
    int using_temp = 0;

    char cmd[4096];
    int rc;

    if (argc < 2) {
        print_usage();
        return 1;
    }

    command = argv[1];

    if (strcmp(command, "--version") == 0 || strcmp(command, "-v") == 0) {
        printf("hosc %s\n", HOSC_CLI_VERSION);
        return 0;
    }

    if (strcmp(command, "version") == 0) {
        printf("hosc %s\n", HOSC_CLI_VERSION);
        return 0;
    }

    if (strcmp(command, "fmt") == 0) {
        return handle_fmt_command(argc, argv);
    }

    if (argc < 3) {
        print_usage();
        return 1;
    }

    input = argv[2];

    for (i = 3; i < argc; i++) {
        if (strcmp(argv[i], "-o") == 0) {
            if (i + 1 >= argc) {
                fprintf(stderr, "Missing value for -o\n");
                return 1;
            }
            out_arg = argv[++i];
        } else if (strcmp(argv[i], "--keep") == 0) {
            keep_output = 1;
        } else {
            fprintf(stderr, "Unknown option: %s\n", argv[i]);
            return 1;
        }
    }

    get_exe_dir(exe_dir, sizeof(exe_dir));
    path_join(exe_dir, "hosc-compiler.exe", compiler_path, sizeof(compiler_path));
    path_join(exe_dir, "hvm.exe", vm_path, sizeof(vm_path));

    if (!file_exists(compiler_path)) snprintf(compiler_path, sizeof(compiler_path), "hosc-compiler.exe");
    if (!file_exists(vm_path)) snprintf(vm_path, sizeof(vm_path), "hvm.exe");

    if (strcmp(command, "build") == 0) {
        int build_raw_bytecode;

        if (out_arg) snprintf(output_path, sizeof(output_path), "%s", out_arg);
        else default_exe_output_path(input, output_path, sizeof(output_path));

        build_raw_bytecode = path_has_ext_ci(output_path, ".hbc");

        if (build_raw_bytecode) {
            snprintf(cmd, sizeof(cmd), "\"%s\" \"%s\" -b \"%s\"", compiler_path, input, output_path);
            rc = run_command(cmd);
            if (rc != 0) return rc;

            printf("Built bytecode: %s\n", output_path);
            return 0;
        }

        if (!make_temp_output(temp_path, sizeof(temp_path))) {
            fprintf(stderr, "Failed to create temp output file\n");
            return 1;
        }

        snprintf(cmd, sizeof(cmd), "\"%s\" \"%s\" -b \"%s\"", compiler_path, input, temp_path);
        rc = run_command(cmd);
        if (rc != 0) {
            DeleteFileA(temp_path);
            return rc;
        }

        rc = bundle_runtime_executable(vm_path, temp_path, output_path);
        DeleteFileA(temp_path);
        if (rc != 0) {
            return rc;
        }

        printf("Built executable: %s\n", output_path);
        return 0;
    }

    if (strcmp(command, "check") == 0) {
        if (!make_temp_output(temp_path, sizeof(temp_path))) {
            fprintf(stderr, "Failed to create temp output file\n");
            return 1;
        }

        snprintf(cmd, sizeof(cmd), "\"%s\" \"%s\" -b \"%s\"", compiler_path, input, temp_path);
        rc = run_command(cmd);
        DeleteFileA(temp_path);

        if (rc != 0) return rc;
        printf("Check OK: %s\n", input);
        return 0;
    }

    if (strcmp(command, "run") == 0) {
        if (out_arg) {
            snprintf(output_path, sizeof(output_path), "%s", out_arg);
        } else {
            if (!make_temp_output(temp_path, sizeof(temp_path))) {
                fprintf(stderr, "Failed to create temp output file\n");
                return 1;
            }
            snprintf(output_path, sizeof(output_path), "%s", temp_path);
            using_temp = 1;
        }

        snprintf(cmd, sizeof(cmd), "\"%s\" \"%s\" -b \"%s\"", compiler_path, input, output_path);
        rc = run_command(cmd);
        if (rc != 0) {
            if (using_temp && !keep_output) DeleteFileA(output_path);
            return rc;
        }

        snprintf(cmd, sizeof(cmd), "\"%s\" \"%s\"", vm_path, output_path);
        rc = run_command(cmd);

        if (using_temp && !keep_output) DeleteFileA(output_path);
        return rc;
    }

    fprintf(stderr, "Unknown command: %s\n", command);
    print_usage();
    return 1;
}
