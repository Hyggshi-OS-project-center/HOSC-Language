/* hosc_gui.c - GUI backend for HOSC framework runtime */

#include "hosc_runtime.h"

#include <stdio.h>
#include <string.h>

#ifdef _WIN32
#include <windows.h>
#include <windowsx.h>
#endif

#define HOSC_GUI_EVENT_QUEUE_CAP 256

typedef struct {
    HOSCGUIBackend backend;
    bool initialized;
    bool running;
    HOSCGUIEvent queue[HOSC_GUI_EVENT_QUEUE_CAP];
    size_t head;
    size_t tail;
#ifdef _WIN32
    HWND window;
    bool class_registered;
#endif
} HOSCGuiState;

static HOSCGuiState* hosc_gui_state(HOSCRuntimeContext* context) {
    return context ? (HOSCGuiState*)context->gui_state : NULL;
}

static void hosc_gui_clear_event_queue(HOSCGuiState* state) {
    if (!state) {
        return;
    }
    state->head = 0;
    state->tail = 0;
}

static bool hosc_gui_event_queue_is_full(HOSCGuiState* state) {
    size_t next_tail;
    if (!state) {
        return false;
    }
    next_tail = (state->tail + 1U) % HOSC_GUI_EVENT_QUEUE_CAP;
    return next_tail == state->head;
}

static void hosc_gui_push_event(HOSCGuiState* state, HOSCGUIEventType type, int key_code, int x, int y, int button) {
    HOSCGUIEvent event;

    if (!state) {
        return;
    }

    event.type = type;
    event.key_code = key_code;
    event.mouse_x = x;
    event.mouse_y = y;
    event.mouse_button = button;

    if (hosc_gui_event_queue_is_full(state)) {
        state->head = (state->head + 1U) % HOSC_GUI_EVENT_QUEUE_CAP;
    }

    state->queue[state->tail] = event;
    state->tail = (state->tail + 1U) % HOSC_GUI_EVENT_QUEUE_CAP;
}

static bool hosc_gui_pop_event(HOSCGuiState* state, HOSCGUIEvent* out_event) {
    if (!state || !out_event) {
        return false;
    }

    if (state->head == state->tail) {
        return false;
    }

    *out_event = state->queue[state->head];
    state->head = (state->head + 1U) % HOSC_GUI_EVENT_QUEUE_CAP;
    return true;
}

#ifdef _WIN32
static LRESULT CALLBACK hosc_gui_wndproc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam) {
    HOSCGuiState* state = (HOSCGuiState*)GetWindowLongPtr(hwnd, GWLP_USERDATA);

    switch (msg) {
        case WM_CLOSE:
            hosc_gui_push_event(state, HOSC_GUI_EVENT_QUIT, 0, 0, 0, 0);
            DestroyWindow(hwnd);
            return 0;
        case WM_DESTROY:
            if (state) {
                state->running = false;
            }
            PostQuitMessage(0);
            return 0;
        case WM_KEYDOWN:
            hosc_gui_push_event(state, HOSC_GUI_EVENT_KEY_DOWN, (int)wparam, 0, 0, 0);
            return 0;
        case WM_KEYUP:
            hosc_gui_push_event(state, HOSC_GUI_EVENT_KEY_UP, (int)wparam, 0, 0, 0);
            return 0;
        case WM_MOUSEMOVE:
            hosc_gui_push_event(state, HOSC_GUI_EVENT_MOUSE_MOVE, 0, GET_X_LPARAM(lparam), GET_Y_LPARAM(lparam), 0);
            return 0;
        case WM_LBUTTONDOWN:
            hosc_gui_push_event(state, HOSC_GUI_EVENT_MOUSE_DOWN, 0, GET_X_LPARAM(lparam), GET_Y_LPARAM(lparam), 0);
            return 0;
        case WM_LBUTTONUP:
            hosc_gui_push_event(state, HOSC_GUI_EVENT_MOUSE_UP, 0, GET_X_LPARAM(lparam), GET_Y_LPARAM(lparam), 0);
            return 0;
        default:
            break;
    }

    return DefWindowProc(hwnd, msg, wparam, lparam);
}
#endif

bool hosc_gui_init(HOSCRuntimeContext* context) {
    HOSCGuiState* state;

    if (!context) {
        return false;
    }

    state = hosc_gui_state(context);
    if (!state) {
        state = (HOSCGuiState*)hosc_allocate(context, sizeof(HOSCGuiState));
        if (!state) {
            return false;
        }
        memset(state, 0, sizeof(HOSCGuiState));
        context->gui_state = state;
    }

    if (state->initialized) {
        return true;
    }

    state->backend = HOSC_GUI_BACKEND_CONSOLE;
#ifdef _WIN32
    state->backend = HOSC_GUI_BACKEND_WIN32;
#endif
    state->initialized = true;
    state->running = false;
    hosc_gui_clear_event_queue(state);

    return true;
}

void hosc_gui_shutdown(HOSCRuntimeContext* context) {
    HOSCGuiState* state = hosc_gui_state(context);
    if (!state) {
        return;
    }

#ifdef _WIN32
    if (state->window) {
        DestroyWindow(state->window);
        state->window = NULL;
    }
#endif

    state->running = false;
    state->initialized = false;
    state->backend = HOSC_GUI_BACKEND_CONSOLE;
    hosc_gui_clear_event_queue(state);

    hosc_deallocate(context, state);
    if (context) {
        context->gui_state = NULL;
    }
}

HOSCGUIBackend hosc_gui_backend(HOSCRuntimeContext* context) {
    HOSCGuiState* state = hosc_gui_state(context);
    if (!state) {
        return HOSC_GUI_BACKEND_CONSOLE;
    }
    return state->backend;
}

const char* hosc_gui_backend_name(HOSCRuntimeContext* context) {
    HOSCGuiState* state = hosc_gui_state(context);
    if (!state) {
        return "console";
    }
    switch (state->backend) {
        case HOSC_GUI_BACKEND_WIN32:
            return "win32";
        case HOSC_GUI_BACKEND_CONSOLE:
        default:
            return "console";
    }
}

bool hosc_gui_create_window(HOSCRuntimeContext* context, const char* title, int width, int height) {
    HOSCGuiState* state = hosc_gui_state(context);

    if (!context) {
        return false;
    }

    if (!state) {
        if (!hosc_gui_init(context)) {
            return false;
        }
        state = hosc_gui_state(context);
    }

#ifdef _WIN32
    if (state->backend == HOSC_GUI_BACKEND_WIN32) {
        if (!state->class_registered) {
            WNDCLASSEX wc;
            ATOM atom;

            memset(&wc, 0, sizeof(wc));
            wc.cbSize = sizeof(wc);
            wc.style = CS_HREDRAW | CS_VREDRAW;
            wc.lpfnWndProc = hosc_gui_wndproc;
            wc.hInstance = GetModuleHandle(NULL);
            wc.hCursor = LoadCursor(NULL, IDC_ARROW);
            wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
            wc.lpszClassName = "HOSCWindowClass";

            atom = RegisterClassEx(&wc);
            if (!atom) {
                DWORD err = GetLastError();
                if (err != ERROR_CLASS_ALREADY_EXISTS) {
                    state->backend = HOSC_GUI_BACKEND_CONSOLE;
                } else {
                    state->class_registered = true;
                }
            } else {
                state->class_registered = true;
            }
        }

        if (state->backend == HOSC_GUI_BACKEND_WIN32) {
            if (state->window) {
                DestroyWindow(state->window);
                state->window = NULL;
            }

            state->window = CreateWindowExA(
                0,
                "HOSCWindowClass",
                title ? title : "HOSC",
                WS_OVERLAPPEDWINDOW,
                CW_USEDEFAULT,
                CW_USEDEFAULT,
                width,
                height,
                NULL,
                NULL,
                GetModuleHandle(NULL),
                NULL
            );

            if (state->window) {
                SetWindowLongPtr(state->window, GWLP_USERDATA, (LONG_PTR)state);
                ShowWindow(state->window, SW_SHOW);
                UpdateWindow(state->window);
                state->running = true;
                hosc_gui_clear_event_queue(state);
                return true;
            }

            state->backend = HOSC_GUI_BACKEND_CONSOLE;
        }
    }
#endif

    state->running = true;
    hosc_gui_clear_event_queue(state);
    return true;
}

void hosc_gui_draw_text(HOSCRuntimeContext* context, int x, int y, const char* text) {
    HOSCGuiState* state = hosc_gui_state(context);

    if (!state) {
        return;
    }

#ifdef _WIN32
    if (state->backend == HOSC_GUI_BACKEND_WIN32 && state->window) {
        HDC dc = GetDC(state->window);
        if (dc) {
            TextOutA(dc, x, y, text ? text : "", (int)strlen(text ? text : ""));
            ReleaseDC(state->window, dc);
        }
        hosc_gui_pump_events(context);
        return;
    }
#endif

    printf("[GUI] (%d,%d) %s\n", x, y, text ? text : "");
}

void hosc_gui_message_box(HOSCRuntimeContext* context, const char* message) {
    (void)context;
#ifdef _WIN32
    MessageBoxA(NULL, message ? message : "", "HOSC", MB_OK | MB_ICONINFORMATION);
#else
    printf("[win32 unavailable] %s\n", message ? message : "");
#endif
}
void hosc_gui_pump_events(HOSCRuntimeContext* context) {
    HOSCGuiState* state = hosc_gui_state(context);

    if (!state) {
        return;
    }

#ifdef _WIN32
    if (state->backend == HOSC_GUI_BACKEND_WIN32) {
        MSG msg;
        while (PeekMessage(&msg, NULL, 0, 0, PM_REMOVE)) {
            TranslateMessage(&msg);
            DispatchMessageA(&msg);
        }
    }
#endif
}

bool hosc_gui_poll_event(HOSCRuntimeContext* context, HOSCGUIEvent* out_event) {
    HOSCGuiState* state = hosc_gui_state(context);
    HOSCGUIEvent empty_event;

    if (!state) {
        return false;
    }

    hosc_gui_pump_events(context);

    if (hosc_gui_pop_event(state, out_event)) {
        return true;
    }

    if (out_event) {
        empty_event.type = HOSC_GUI_EVENT_NONE;
        empty_event.key_code = 0;
        empty_event.mouse_x = 0;
        empty_event.mouse_y = 0;
        empty_event.mouse_button = 0;
        *out_event = empty_event;
    }

    return false;
}

bool hosc_gui_is_running(HOSCRuntimeContext* context) {
    HOSCGuiState* state = hosc_gui_state(context);
    if (!state) {
        return false;
    }
    return state->running;
}



