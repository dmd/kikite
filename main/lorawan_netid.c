#include "lorawan_netid.h"
#include <stddef.h>

static const uint8_t nwkid_bits[8] = {6, 6, 9, 11, 12, 13, 15, 17};

typedef struct {
    uint32_t    netid;
    const char* name;
} network_operator_t;

static const network_operator_t network_operators[] = {
    {0x000000, "Experimental"},
    {0x000001, "Experimental"},
    {0x000002, "Actility"},
    {0x600013, "Actility"},
    {0x000009, "Netmore"},
    {0x00000A, "KPN"},
    {0x60001F, "KPN"},
    {0x00000B, "Everynet"},
    {0x00000F, "Orange"},
    {0x600032, "Orange"},
    {0x000013, "TTN"},
    {0xC00058, "TTN"},
    {0x000018, "Loriot"},
    {0x000022, "Comcast"},
    {0x000024, "Helium (old)"},
    {0x60001C, "MachineQ"},
    {0x000039, "Amazon"},
    {0x60002B, "Amazon"},
    {0x00003B, "Semtech"},
    {0x00003C, "Helium"},
    {0x60002D, "Helium"},
    {0xC00053, "Helium"},
    {0x600010, "Senet"},
};

bool lorawan_netid_from_devaddr(uint32_t dev_addr, lorawan_netid_t* out) {
    uint8_t type = 0;
    while (type < 8 && (dev_addr & (0x80000000u >> type))) {
        type++;
    }
    if (type >= 8) {
        return false;
    }
    uint8_t bits = nwkid_bits[type];
    out->type    = type;
    out->nwkid   = (dev_addr << (type + 1)) >> (32 - bits);
    out->netid   = (uint32_t)type << 21 | out->nwkid;
    return true;
}

const char* lorawan_network_operator_name(uint32_t netid) {
    for (size_t i = 0; i < sizeof(network_operators) / sizeof(network_operators[0]); i++) {
        if (network_operators[i].netid == netid) {
            return network_operators[i].name;
        }
    }
    return NULL;
}
