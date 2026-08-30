#include "hvm_api.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "hvm_object.h"

#ifdef _WIN32
#include <windows.h>
#include <mmsystem.h>
#else
#include <fcntl.h>
#include <unistd.h>
#include <sys/wait.h>
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

#ifndef _WIN32
/*
 * POSIX (Linux/macOS) audio backend.
 *
 * The VM does not link any audio/decoding library (SDL2, OpenAL, PulseAudio,
 * PipeWire, ...), so instead of duplicating an MP3/OGG/WAV decoder here, we
 * delegate playback to whichever media player the system already has on
 * PATH. This mirrors what the Windows path effectively does too: it asks the
 * OS (via MCI) to open and decode the file rather than doing it itself.
 *
 * Returns:
 *   0   - played successfully
 *   1   - the player exists but reported a failure (bad file, codec, etc.)
 *   127 - the player is not installed; caller should try the next candidate
 *  -1   - fork()/waitpid() itself failed (fatal, stop trying)
 */
static int hvm_audio_try_player(char* const argv_list[]) {
    pid_t pid;
    int status;

    pid = fork();
    if (pid < 0) {
        return -1;
    }

    if (pid == 0) {
        /* Child: silence the player's own console output; HOSC scripts
         * shouldn't see a media player's logging spill onto stdout/stderr. */
        int devnull = open("/dev/null", O_WRONLY);
        if (devnull >= 0) {
            dup2(devnull, STDOUT_FILENO);
            dup2(devnull, STDERR_FILENO);
            if (devnull > STDERR_FILENO) {
                close(devnull);
            }
        }
        execvp(argv_list[0], argv_list);
        /* execvp only returns on failure (e.g. binary not found on PATH) */
        _exit(127);
    }

    if (waitpid(pid, &status, 0) < 0) {
        return -1;
    }

    if (WIFEXITED(status)) {
        int code = WEXITSTATUS(status);
        if (code == 127) {
            return 127;
        }
        return (code == 0) ? 0 : 1;
    }

    /* Killed by a signal, etc. Treat as "player was present but failed". */
    return 1;
}

/* Tries a chain of common Linux/macOS media players, in order, until one of
 * them is actually installed and can play the file. Players capable of
 * decoding compressed formats (mp3/ogg/etc.) are tried first; the plain PCM
 * players (paplay/aplay) are last-resort fallbacks for raw WAV files. */
static bool hvm_audio_play_posix(const char* path) {
    char* ffplay_argv[]  = {"ffplay", "-nodisp", "-autoexit", "-loglevel", "quiet", (char*)path, NULL};
    char* mpv_argv[]     = {"mpv", "--no-video", "--really-quiet", (char*)path, NULL};
    char* mpg123_argv[]  = {"mpg123", "-q", (char*)path, NULL};
    char* cvlc_argv[]    = {"cvlc", "--play-and-exit", "-q", "--no-osd", "-Idummy", (char*)path, NULL};
    char* paplay_argv[]  = {"paplay", (char*)path, NULL};
    char* aplay_argv[]   = {"aplay", "-q", (char*)path, NULL};
#ifdef __APPLE__
    char* afplay_argv[]  = {"afplay", (char*)path, NULL};
#endif

    char** candidates[] = {
#ifdef __APPLE__
        afplay_argv,
#endif
        ffplay_argv,
        mpv_argv,
        mpg123_argv,
        cvlc_argv,
        paplay_argv,
        aplay_argv
    };
    size_t candidate_count = sizeof(candidates) / sizeof(candidates[0]);
    size_t i;
    bool found_any_player = false;

    for (i = 0; i < candidate_count; ++i) {
        int rc = hvm_audio_try_player(candidates[i]);
        if (rc == 127) {
            continue; /* not installed, try the next backend */
        }
        found_any_player = true;
        if (rc == 0) {
            return true;
        }
        /* Installed but failed on this file (e.g. wrong codec for
         * paplay/aplay) - keep trying the remaining candidates. */
    }

    if (!found_any_player) {
        fprintf(stderr,
            "[audio] no audio player found on this system for \"%s\".\n"
            "[audio] install one of: ffmpeg (ffplay), mpv, mpg123, vlc (cvlc), "
            "or PulseAudio/ALSA utils (paplay/aplay) - no HOSC code changes "
            "needed once one is on PATH.\n",
            path);
    } else {
        fprintf(stderr, "[audio] failed to play \"%s\" with any available player\n", path);
    }

    return false;
}
#endif /* !_WIN32 */

static bool hvm_native_audio_play(HVM* vm, int argc, const HValue* argv, HValue* out_result) {
    const char* path;
#ifdef _WIN32
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
    {
        MCI_OPEN_PARMSA open_parms;
        MCI_PLAY_PARMS  play_parms;
        MCIDEVICEID device_id;

        /* Reject paths containing embedded quotes which could confuse callers
         * that still use mciSendStringA elsewhere. */
        if (strchr(path, '"') != NULL) {
            fputs("[audio] audio.play: path must not contain '\"'\n", stderr);
            *out_result = hvm_value_bool(false);
            return true;
        }

        /* Use mciSendCommandA (struct-based, not string-based) so the file
         * path is never interpolated into a shell command string, eliminating
         * MCI command injection via crafted filenames. */
        mciSendCommandA(0, MCI_CLOSE, 0, (DWORD_PTR)NULL); /* clear stale handle */

        memset(&open_parms, 0, sizeof(open_parms));
        open_parms.lpstrDeviceType = "mpegvideo";
        open_parms.lpstrElementName = path;

        err = mciSendCommandA(0, MCI_OPEN,
                              MCI_OPEN_TYPE | MCI_OPEN_ELEMENT,
                              (DWORD_PTR)&open_parms);
        if (err != 0) {
            mciGetErrorStringA(err, error_text, sizeof(error_text));
            fprintf(stderr, "[audio] failed to open \"%s\": %s\n", path, error_text);
            *out_result = hvm_value_bool(false);
            return true;
        }

        device_id = open_parms.wDeviceID;
        memset(&play_parms, 0, sizeof(play_parms));
        err = mciSendCommandA(device_id, MCI_PLAY, MCI_WAIT, (DWORD_PTR)&play_parms);
        mciSendCommandA(device_id, MCI_CLOSE, 0, (DWORD_PTR)NULL);
        if (err != 0) {
            mciGetErrorStringA(err, error_text, sizeof(error_text));
            fprintf(stderr, "[audio] failed to play \"%s\": %s\n", path, error_text);
            *out_result = hvm_value_bool(false);
            return true;
        }
    }

    *out_result = hvm_value_bool(true);
    return true;
#else
    *out_result = hvm_value_bool(hvm_audio_play_posix(path));
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
