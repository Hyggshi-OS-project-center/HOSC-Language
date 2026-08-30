/* runtime_gui_backend.c - Backend registry and selection for HOSC GUI */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "runtime_gui_backend.h"

#define MAX_BACKENDS 8

static HVM_GuiBackend* g_backends[MAX_BACKENDS];
static int g_backend_count = 0;
static const HVM_GuiBackend* g_selected = NULL;

int hvm_gui_backend_register(HVM_GuiBackend* backend) {
    if (!backend || g_backend_count >= MAX_BACKENDS) return 0;
    g_backends[g_backend_count++] = backend;
    return 1;
}

const HVM_GuiBackend* hvm_gui_backend_select(void) {
    int i;
    
    /* Try registered backends in order, pick first available */
    for (i = 0; i < g_backend_count; i++) {
        if (g_backends[i]->available && g_backends[i]->available()) {
            g_selected = g_backends[i];
            if (g_selected->init) g_selected->init();
            return g_selected;
        }
    }
    
    /* Fallback to console */
    g_selected = &hvm_gui_backend_console;
    if (g_selected->init) g_selected->init();
    return g_selected;
}

const HVM_GuiBackend* hvm_gui_backend_get(void) {
    return g_selected;
}