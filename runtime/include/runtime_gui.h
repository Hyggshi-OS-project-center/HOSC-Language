/*
 * File: runtime/include/runtime_gui.h
 * Purpose: Declares GUI state creation and service binding helpers.
 */

#ifndef RUNTIME_GUI_H
#define RUNTIME_GUI_H

#ifdef __cplusplus
extern "C" {
#endif

typedef struct HVM_RuntimeServices HVM_RuntimeServices;
typedef struct HVM_GuiState HVM_GuiState;

HVM_GuiState *hvm_gui_create_state(void);
void hvm_gui_destroy_state(HVM_GuiState *state);
void hvm_gui_bind_services(HVM_RuntimeServices *services, HVM_GuiState *state);

#ifdef __cplusplus
}
#endif

#endif