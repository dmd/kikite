#pragma once
#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"
typedef struct {
    bool   battery_available;
    double remaining_percentage;
} bsp_power_battery_information_t;
static inline esp_err_t bsp_power_get_battery_information(bsp_power_battery_information_t* info) {
    info->battery_available    = true;
    info->remaining_percentage = 87.0;
    return ESP_OK;
}
