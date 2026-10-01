#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"
#include "lora.h"

#define RADIO_MAX_HOP_CHANNELS 64

typedef enum {
    SWEEP_RETUNE_ONLY,
    SWEEP_RETUNE_THEN_RX,
    SWEEP_STANDBY_RETUNE_RX,
    SWEEP_METHOD_COUNT,
} sweep_method_t;

typedef struct {
    uint32_t       start_hz;
    uint32_t       step_hz;
    uint16_t       bins;
    uint16_t       bandwidth_khz;
    uint8_t        samples_per_step;
    uint16_t       settle_us;
    sweep_method_t method;
} sweep_params_t;

typedef struct {
    uint32_t frequency_hz;
    uint8_t  spreading_factor;
    uint16_t bandwidth_khz;
    uint8_t  coding_rate;
    uint8_t  sync_word;
    uint16_t preamble_length;
    bool     invert_iq;
} listen_channel_t;

typedef struct {
    listen_channel_t channels[RADIO_MAX_HOP_CHANNELS];
    uint8_t          channel_count;
    uint32_t         dwell_ms;
} listen_params_t;

typedef struct {
    uint32_t mode_us;
    uint32_t config_us;
    uint32_t rssi_us;
    uint32_t step_us;
} sweep_timing_t;

typedef struct {
    bool                 available;
    bool                 rssi_supported;
    bool                 rssi_unsupported;
    lora_protocol_chip_t chip_type;
    char                 version[LORA_PROTOCOL_VERSION_STRING_LENGTH + 1];
    uint16_t             device_errors;
    uint32_t             rpc_errors;
    sweep_timing_t       timing;
    uint32_t             last_sweep_ms;
    uint8_t              listen_channel_index;
    uint32_t             listen_frequency_hz;
} radio_info_t;

esp_err_t    radio_start(void);
void         radio_request_idle(void);
void         radio_request_sweep(const sweep_params_t* params);
void         radio_request_listen(const listen_params_t* params);
void         radio_get_info(radio_info_t* out_info);
const char*  radio_sweep_method_name(sweep_method_t method);
void         radio_restore_and_stop(void);
