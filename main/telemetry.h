#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "capture.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "radio.h"

bool telemetry_start(QueueHandle_t key_queue);
bool telemetry_enabled(void);
void telemetry_sweep(const sweep_params_t* params, uint32_t sweep_ms);
void telemetry_packet(const captured_packet_t* packet);
void telemetry_framebuffer(const uint8_t* pixels, int raw_width, int raw_height);
