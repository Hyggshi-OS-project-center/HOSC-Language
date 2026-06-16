#include "hvm_api.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "hvm_object.h"

#ifdef _WIN32
#include <windows.h>
#include <mmsystem.h>
#endif

static bool hvm_native_print(HVM* vm, int argc, const HValue* argv, HValue* out_result) {
    int i;
    (void)vm;

    for (i = 0; i < argc; ++i) {
        if (i > 0) {
            fputc(' ', stdout);
        }
        hvm_value_print(argv[i]);
    }
    fputc('\n', stdout);
    *out_result = hvm_value_nil();
    return true;
}

static bool hvm_native_audio_play(HVM* vm, int argc, const HValue* argv, HValue* out_result) {
    const char* path;
#ifdef _WIN32
    char command[2048];
    char error_text[256];
    MCIERROR err;
#endif
    (void)vm;

    if (argc < 1 || argv[0].tag != HVAL_OBJ || !argv[0].as.object ||
        argv[0].as.object->kind != HOBJ_STRING) {
        fputs("[audio] audio.play expects a string path\n", stderr);
        *out_result = hvm_value_bool(false);
        return true;
    }

    path = ((HStringObject*)argv[0].as.object)->chars;
    if (!path || !path[0]) {
        fputs("[audio] audio.play src is empty\n", stderr);
        *out_result = hvm_value_bool(false);
        return true;
    }

#ifdef _WIN32
    mciSendStringA("close hosc_audio", NULL, 0, NULL);
    snprintf(command, sizeof(command), "open \"%s\" type mpegvideo alias hosc_audio", path);
    err = mciSendStringA(command, NULL, 0, NULL);
    if (err != 0) {
        mciGetErrorStringA(err, error_text, sizeof(error_text));
        fprintf(stderr, "[audio] failed to open \"%s\": %s\n", path, error_text);
        *out_result = hvm_value_bool(false);
        return true;
    }

    err = mciSendStringA("play hosc_audio wait", NULL, 0, NULL);
    mciSendStringA("close hosc_audio", NULL, 0, NULL);
    if (err != 0) {
        mciGetErrorStringA(err, error_text, sizeof(error_text));
        fprintf(stderr, "[audio] failed to play \"%s\": %s\n", path, error_text);
        *out_result = hvm_value_bool(false);
        return true;
    }

    *out_result = hvm_value_bool(true);
    return true;
#else
    fprintf(stderr, "[audio] audio.play is only implemented on Windows: %s\n", path);
    *out_result = hvm_value_bool(false);
    return true;
#endif
}

bool hvm_register_native(HVM* vm, const char* name, int arity, HNativeFn fn) {
    HNativeRegistryEntry* items;
    HNativeObject* native_object;
    size_t new_capacity;

    if (vm->native_count == vm->native_capacity) {
        new_capacity = vm->native_capacity == 0 ? 4 : vm->native_capacity * 2;
        items = (HNativeRegistryEntry*)realloc(vm->natives, new_capacity * sizeof(HNativeRegistryEntry));
        if (!items) {
            hvm_set_error(vm, "failed to grow native registry");
            return false;
        }
        vm->natives = items;
        vm->native_capacity = new_capacity;
    }

    native_object = hvm_native_new(vm, name, arity, fn);
    if (!native_object) {
        hvm_set_error(vm, "failed to allocate native object");
        return false;
    }

    vm->natives[vm->native_count++].object = native_object;
    return true;
}

HNativeObject* hvm_lookup_native(HVM* vm, const char* name) {
    size_t i;

    for (i = 0; i < vm->native_count; ++i) {
        if (strcmp(vm->natives[i].object->name, name) == 0) {
            return vm->natives[i].object;
        }
    }
    return NULL;
}

void hvm_register_builtin_natives(HVM* vm) {
    hvm_register_native(vm, "print", -1, hvm_native_print);
    hvm_register_native(vm, "audio.play", 1, hvm_native_audio_play);
}
