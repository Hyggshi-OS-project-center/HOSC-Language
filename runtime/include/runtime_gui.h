/* runtime_gui.h - GUI services for HOSC VM runtime */
#ifndef RUNTIME_GUI_H
#define RUNTIME_GUI_H

#ifdef __cplusplus
extern "C" {
#endif

#include "hvm.h"

typedef struct HVM_GuiState HVM_GuiRuntime;

HVM_GuiRuntime* hvm_gui_create(void);
void hvm_gui_destroy(HVM_GuiRuntime* gui);
void hvm_gui_reset_for_run(HVM_GuiRuntime* gui);
int hvm_gui_handle_opcode(HVM_GuiRuntime* gui, HVM_VM* vm, HVM_Opcode opcode, const HVM_Instruction* instr);
void hvm_gui_post_run(HVM_GuiRuntime* gui, HVM_VM* vm);

#ifdef __cplusplus
}
#endif

#endif /* RUNTIME_GUI_H */
