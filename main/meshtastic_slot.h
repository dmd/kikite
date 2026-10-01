#pragma once

#include <stdint.h>

static inline uint32_t meshtastic_djb2(const char* text) {
    uint32_t hash = 5381;
    for (const unsigned char* c = (const unsigned char*)text; *c; c++) {
        hash = (hash << 5) + hash + *c;
    }
    return hash;
}

static inline uint32_t meshtastic_default_frequency(uint32_t region_start_hz, uint32_t region_end_hz,
                                                    uint16_t bandwidth_khz, const char* channel_name) {
    uint32_t bandwidth_hz = bandwidth_khz == 62 ? 62500 : (uint32_t)bandwidth_khz * 1000;
    uint32_t slots        = (region_end_hz - region_start_hz + bandwidth_hz / 2) / bandwidth_hz;
    if (slots == 0) {
        slots = 1;
    }
    uint32_t slot = meshtastic_djb2(channel_name) % slots;
    return region_start_hz + bandwidth_hz / 2 + slot * bandwidth_hz;
}
