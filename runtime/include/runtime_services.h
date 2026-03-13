/* runtime_services.h - Runtime service interface for HOSC VM */
#ifndef RUNTIME_SERVICES_H
#define RUNTIME_SERVICES_H

#ifdef __cplusplus
extern "C" {
#endif

#include "hvm.h"

typedef struct HVM_RuntimeServices HVM_RuntimeServices;

HVM_RuntimeServices* hvm_runtime_services_create_default(void);
void hvm_runtime_services_destroy(HVM_RuntimeServices* services);
void hvm_runtime_services_reset_for_run(HVM_RuntimeServices* services);
int hvm_runtime_services_handle_gui_opcode(HVM_RuntimeServices* services, HVM_VM* vm, HVM_Opcode opcode, const HVM_Instruction* instr);
void hvm_runtime_services_post_run(HVM_RuntimeServices* services, HVM_VM* vm);

#ifdef __cplusplus
}
#endif

#endif /* RUNTIME_SERVICES_H */
