#pragma once

#include <stdint.h>
#include "radio.h"

typedef enum {
    REGION_US915,
    REGION_EU868,
    REGION_433,
    REGION_COUNT,
} region_t;

typedef struct {
    const char* name;
    uint32_t    start_hz;
    uint32_t    stop_hz;
} sweep_range_t;

typedef struct {
    const char* name;
    void (*build)(listen_params_t* out, uint32_t cursor_hz);
} listen_profile_t;

const char*             profiles_region_name(region_t region);
const sweep_range_t*    profiles_sweep_ranges(region_t region, uint8_t* out_count);
const listen_profile_t* profiles_listen(region_t region, uint8_t* out_count);
