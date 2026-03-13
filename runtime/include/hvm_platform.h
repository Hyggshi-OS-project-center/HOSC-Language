/* hvm_platform.h - Platform helpers for HOSC VM runtime */

#ifndef HVM_PLATFORM_H
#define HVM_PLATFORM_H

#include "hvm.h"

#ifdef __cplusplus
extern "C" {
#endif

void hvm_platform_hide_console_if_needed(const HVM_Instruction* code, size_t count);

#ifdef __cplusplus
}
#endif

#endif /* HVM_PLATFORM_H */
