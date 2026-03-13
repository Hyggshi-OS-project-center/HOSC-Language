/*
 * File: runtime/src/runtime_services.c
 * Purpose: Provides default runtime service wiring for the HVM.
 */

#include <stdlib.h>
#include "runtime_services.h"
#include "runtime_gui.h"

HVM_RuntimeServices *hvm_runtime_services_create_default(void) {
    HVM_RuntimeServices *services = (HVM_RuntimeServices *)calloc(1, sizeof(HVM_RuntimeServices));
    HVM_GuiState *gui_state = NULL;
    if (!services) return NULL;

    gui_state = hvm_gui_create_state();
    if (!gui_state) {
        free(services);
        return NULL;
    }

    services->context = gui_state;
    hvm_gui_bind_services(services, gui_state);
    return services;
}

void hvm_runtime_services_destroy(HVM_RuntimeServices *services) {
    if (!services) return;
    if (services->context) {
        hvm_gui_destroy_state((HVM_GuiState *)services->context);
        services->context = NULL;
    }
    free(services);
}