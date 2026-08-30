#include "hvm_api.h"

uint16_t hvm_read_u16(const uint8_t* ip) {
    return (uint16_t)(ip[0] | ((uint16_t)ip[1] << 8));
}
