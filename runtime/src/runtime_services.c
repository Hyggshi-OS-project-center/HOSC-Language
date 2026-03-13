/* runtime_services.c - Default runtime service wiring for HOSC VM */

#include <stdlib.h>

#include "runtime_services.h"
#include "runtime_gui.h"

struct HVM_RuntimeServices {
    HVM_GuiRuntime *gui;
};

HVM_RuntimeServices* hvm_runtime_services_create_default(void) {
    HVM_RuntimeServices *services = (HVM_RuntimeServices *)calloc(1, sizeof(HVM_RuntimeServices));
    if (!services) return NULL;

    services->gui = hvm_gui_create();
    if (!services->gui) {
        free(services);
        return NULL;
    }

    return services;
}

void hvm_runtime_services_destroy(HVM_RuntimeServices* services) {
    if (!services) return;
    if (services->gui) {
        hvm_gui_destroy(services->gui);
        services->gui = NULL;
    }
    free(services);
}

void hvm_runtime_services_reset_for_run(HVM_RuntimeServices* services) {
    if (!services || !services->gui) return;
    hvm_gui_reset_for_run(services->gui);
}

int hvm_runtime_services_handle_gui_opcode(HVM_RuntimeServices* services, HVM_VM* vm, HVM_Opcode opcode, const HVM_Instruction* instr) {
    if (!services || !services->gui) return 0;
    return hvm_gui_handle_opcode(services->gui, vm, opcode, instr);
}

void hvm_runtime_services_post_run(HVM_RuntimeServices* services, HVM_VM* vm) {
    if (!services || !services->gui) return;
    hvm_gui_post_run(services->gui, vm);
}
