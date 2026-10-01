#pragma once

#include <stdbool.h>
#include <stdint.h>

typedef struct {
    uint8_t  type;
    uint32_t nwkid;
    uint32_t netid;
} lorawan_netid_t;

bool        lorawan_netid_from_devaddr(uint32_t dev_addr, lorawan_netid_t* out);
const char* lorawan_network_operator_name(uint32_t netid);
