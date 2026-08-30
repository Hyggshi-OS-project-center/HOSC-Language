#include "hosc_runtime_api.h"

int hosc_bundle_stub_main(const char* bytecode_path) {
    return hosc_runtime_run_file(bytecode_path, NULL);
}
