#include "hosc_cli.h"

#include <string.h>

#ifdef _WIN32
#include <windows.h>
#endif

static void configure_console_encoding(void) {
#ifdef _WIN32
    DWORD mode;
    if (GetConsoleMode(GetStdHandle(STD_OUTPUT_HANDLE), &mode)) {
        SetConsoleOutputCP(CP_UTF8);
    }
    if (GetConsoleMode(GetStdHandle(STD_ERROR_HANDLE), &mode)) {
        SetConsoleCP(CP_UTF8);
    }
#endif
}

static int parse_run(int argc, char** argv) {
    const char* input;
    const char* out_hbc = NULL;
    int keep = 0;
    int i;

    if (argc < 3) {
        return -1;
    }
    input = argv[2];
    for (i = 3; i < argc; i++) {
        if (strcmp(argv[i], "-o") == 0) {
            if (i + 1 >= argc) {
                return -1;
            }
            out_hbc = argv[++i];
        } else if (strcmp(argv[i], "--keep") == 0) {
            keep = 1;
        } else {
            return -1;
        }
    }
    return hosc_cli_command_run_ex(input, out_hbc, keep);
}

static int parse_fmt(int argc, char** argv) {
    const char* output = NULL;
    int check = 0;
    int i;
    if (argc < 3) return -1;
    for (i = 3; i < argc; ++i) {
        if (strcmp(argv[i], "--check") == 0) check = 1;
        else if (strcmp(argv[i], "-o") == 0 && i + 1 < argc) output = argv[++i];
        else return -1;
    }
    return hosc_cli_command_fmt_ex(argv[2], output, check);
}

static int parse_build(int argc, char** argv) {
    const char* output = NULL;
    if (argc < 3) return -1;
    if (argc == 5 && strcmp(argv[3], "-o") == 0) output = argv[4];
    else if (argc != 3) return -1;
    return hosc_cli_command_build_ex(argv[2], output);
}

int main(int argc, char** argv) {
    configure_console_encoding();

    if (argc < 2) {
        hosc_cli_print_usage();
        return 1;
    }

    if (strcmp(argv[1], "run") == 0) {
        int rc = parse_run(argc, argv);
        if (rc < 0) {
            hosc_cli_print_usage();
            return 1;
        }
        return rc;
    }
    if (strcmp(argv[1], "build") == 0) {
        int rc = parse_build(argc, argv);
        if (rc < 0) { hosc_cli_print_usage(); return 1; }
        return rc;
    }
    if (strcmp(argv[1], "check") == 0) {
        return argc >= 3 ? hosc_cli_command_check(argv[2]) : 1;
    }
    if (strcmp(argv[1], "fmt") == 0) {
        int rc = parse_fmt(argc, argv);
        if (rc < 0) { hosc_cli_print_usage(); return 1; }
        return rc;
    }
    if (strcmp(argv[1], "test") == 0) {
        return hosc_cli_command_test();
    }
    if (strcmp(argv[1], "version") == 0 || strcmp(argv[1], "--version") == 0) {
        return hosc_cli_command_version();
    }

    hosc_cli_print_usage();
    return 1;
}
