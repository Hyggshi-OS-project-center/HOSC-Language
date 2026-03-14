/* runtime_services.h - Runtime service interface for HOSC VM */
#ifndef RUNTIME_SERVICES_H
#define RUNTIME_SERVICES_H

#ifdef __cplusplus
extern "C" {
#endif

#include "hvm.h"

typedef struct HVM_RuntimeServices {
    void *userdata;
    int (*call_native)(void *userdata, HVM_VM *vm, const char *name, const HVM_Instruction *instr);
    void (*reset_for_run)(void *userdata);
    void (*post_run)(void *userdata, HVM_VM *vm);
    void (*sleep_ms)(void *userdata, int ms);
} HVM_RuntimeServices;

HVM_RuntimeServices* hvm_runtime_services_create_default(void);
void hvm_runtime_services_destroy(HVM_RuntimeServices* services);
void hvm_runtime_services_reset_for_run(HVM_RuntimeServices* services);
int hvm_runtime_services_call_native(HVM_RuntimeServices* services, HVM_VM* vm, const char* name, const HVM_Instruction* instr);
void hvm_runtime_services_post_run(HVM_RuntimeServices* services, HVM_VM* vm);
void hvm_runtime_services_sleep_ms(HVM_RuntimeServices* services, int ms);

#ifdef __cplusplus
}
#endif

#endif /* RUNTIME_SERVICES_H */
