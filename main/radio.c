#include "radio.h"
#include <string.h>
#include "capture.h"
#include "esp_log.h"
#include "esp_rom_sys.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "spectrum.h"
#include "telemetry.h"

static const char TAG[] = "radio";

#define PACKET_QUEUE_LENGTH   32
#define RSSI_PROBE_FAILURES   8
#define RETUNE_DRAIN_MS       30
#define IDLE_BIT              BIT0
#define STOPPED_BIT           BIT1
#define RESTORE_ATTEMPTS      3
#define DRIVER_RECAL_HZ       20000000u
#define RECAL_MARGIN_HZ       100000u
#define RECAL_JUMP_HZ         40000000u

typedef enum {
    ACTIVITY_IDLE,
    ACTIVITY_SWEEP,
    ACTIVITY_LISTEN,
    ACTIVITY_STOPPED,
} activity_t;

static lora_handle_t                 lora;
static TaskHandle_t                  radio_task_handle;
static SemaphoreHandle_t             request_mutex;
static EventGroupHandle_t            radio_events;
static activity_t                    requested_activity = ACTIVITY_IDLE;
static uint32_t                      request_generation = 0;
static sweep_params_t                requested_sweep;
static listen_params_t               requested_listen;
static lora_protocol_config_params_t original_config;
static lora_protocol_mode_t          original_mode = LORA_PROTOCOL_MODE_UNKNOWN;
static bool                          original_offset_known = false;
static float                         original_offset_hz    = 0;
static radio_info_t                  info;
static portMUX_TYPE                  info_lock = portMUX_INITIALIZER_UNLOCKED;

static uint32_t current_generation(void) {
    xSemaphoreTake(request_mutex, portMAX_DELAY);
    uint32_t generation = request_generation;
    xSemaphoreGive(request_mutex);
    return generation;
}

static bool request_changed(uint32_t generation) {
    return current_generation() != generation;
}

static void count_rpc_error(esp_err_t res, const char* operation) {
    ESP_LOGW(TAG, "%s failed: %s", operation, esp_err_to_name(res));
    taskENTER_CRITICAL(&info_lock);
    info.rpc_errors++;
    taskEXIT_CRITICAL(&info_lock);
}

static uint32_t smooth(uint32_t average, uint32_t sample) {
    return average == 0 ? sample : (average * 7 + sample) / 8;
}

static lora_protocol_config_params_t base_config(void) {
    lora_protocol_config_params_t config = original_config;
    config.spreading_factor              = 7;
    config.bandwidth                     = 125;
    config.coding_rate                   = 5;
    config.sync_word                     = 0x12;
    config.preamble_length               = 8;
    config.crc_enabled                   = true;
    config.invert_iq                     = false;
    config.low_data_rate_optimization    = false;
    config.rx_boost                      = true;
    config.use_automatic_correction      = false;
    return config;
}

static esp_err_t enter_standby(void) {
    esp_err_t res = lora_set_mode(&lora, LORA_PROTOCOL_MODE_STANDBY_RC);
    if (res != ESP_OK) {
        count_rpc_error(res, "set standby");
    }
    return res;
}

static void discard_packets(void) {
    lora_protocol_lora_packet_t packet;
    while (lora_receive_packet(&lora, &packet, 0) == ESP_OK) {
    }
}

static uint32_t distance_hz(uint32_t a, uint32_t b) {
    return a > b ? a - b : b - a;
}

static void calibrate_at(lora_protocol_config_params_t* config, uint32_t frequency_hz) {
    enter_standby();
    config->frequency = frequency_hz >= 450000000u ? frequency_hz - RECAL_JUMP_HZ : frequency_hz + RECAL_JUMP_HZ;
    lora_set_config(&lora, config);
    config->frequency = frequency_hz;
}

static void refresh_device_errors(void) {
    lora_protocol_status_params_t status = {0};
    if (lora_get_status(&lora, &status) != ESP_OK) {
        return;
    }
    taskENTER_CRITICAL(&info_lock);
    bool changed       = status.errors != info.device_errors;
    info.device_errors = status.errors;
    taskEXIT_CRITICAL(&info_lock);
    if (changed) {
        ESP_LOGW(TAG, "Radio device errors now 0x%04x", status.errors);
    }
}

static bool run_sweep(const sweep_params_t* params, uint32_t generation) {
    lora_protocol_config_params_t config = base_config();
    config.bandwidth                     = params->bandwidth_khz;

    uint8_t  samples       = params->samples_per_step ? params->samples_per_step : 1;
    bool     in_rx         = false;
    uint32_t rssi_failures = 0;

    while (!request_changed(generation)) {
        spectrum_begin_sweep(params->start_hz, params->step_hz, params->bins);
        int64_t  sweep_start   = esp_timer_get_time();
        uint32_t calibrated_hz = params->start_hz;
        calibrate_at(&config, params->start_hz);
        in_rx = false;

        for (uint16_t bin = 0; bin < params->bins; bin++) {
            if (request_changed(generation)) {
                return true;
            }
            int64_t   step_start = esp_timer_get_time();
            uint32_t  mode_us    = 0;
            esp_err_t res        = ESP_OK;

            uint32_t frequency     = params->start_hz + (uint32_t)bin * params->step_hz;
            bool     recalibrating = distance_hz(frequency, calibrated_hz) >= DRIVER_RECAL_HZ - RECAL_MARGIN_HZ;
            if (recalibrating) {
                calibrate_at(&config, frequency);
                calibrated_hz = frequency;
                in_rx         = false;
            } else if (params->method == SWEEP_STANDBY_RETUNE_RX) {
                int64_t t = esp_timer_get_time();
                enter_standby();
                in_rx    = false;
                mode_us += esp_timer_get_time() - t;
            }

            config.frequency = frequency;
            int64_t t        = esp_timer_get_time();
            res              = lora_set_config(&lora, &config);
            uint32_t config_us = esp_timer_get_time() - t;
            if (res != ESP_OK) {
                count_rpc_error(res, "set config");
                spectrum_store_bin(bin, SPECTRUM_NO_DATA);
                continue;
            }

            if (params->method != SWEEP_RETUNE_ONLY || !in_rx) {
                t   = esp_timer_get_time();
                res = lora_set_mode(&lora, LORA_PROTOCOL_MODE_RX);
                mode_us += esp_timer_get_time() - t;
                if (res != ESP_OK) {
                    count_rpc_error(res, "set rx");
                    spectrum_store_bin(bin, SPECTRUM_NO_DATA);
                    in_rx = false;
                    continue;
                }
                in_rx = true;
            }

            if (params->settle_us) {
                esp_rom_delay_us(params->settle_us);
            }

            float    strongest = -1000.0f;
            uint32_t rssi_us   = 0;
            uint8_t  readings  = 0;
            for (uint8_t sample = 0; sample < samples; sample++) {
                if (request_changed(generation)) {
                    return true;
                }
                float dbm = 0;
                t         = esp_timer_get_time();
                res       = lora_get_rssi_inst(&lora, &dbm);
                rssi_us  += esp_timer_get_time() - t;
                if (res == ESP_OK) {
                    readings++;
                    if (dbm > strongest) {
                        strongest = dbm;
                    }
                } else {
                    count_rpc_error(res, "get rssi");
                    if (res == ESP_FAIL) {
                        rssi_failures++;
                    }
                }
            }

            taskENTER_CRITICAL(&info_lock);
            if (readings) {
                info.rssi_supported   = true;
                info.rssi_unsupported = false;
            }
            info.timing.mode_us   = smooth(info.timing.mode_us, mode_us);
            info.timing.config_us = smooth(info.timing.config_us, config_us);
            info.timing.rssi_us   = smooth(info.timing.rssi_us, rssi_us / samples);
            info.timing.step_us   = smooth(info.timing.step_us, esp_timer_get_time() - step_start);
            bool probe_failed     = !info.rssi_supported && rssi_failures >= RSSI_PROBE_FAILURES;
            if (probe_failed) {
                info.rssi_unsupported = true;
            }
            taskEXIT_CRITICAL(&info_lock);

            spectrum_store_bin(bin, readings ? (int16_t)(strongest * 2.0f) : SPECTRUM_NO_DATA);

            if (probe_failed) {
                ESP_LOGE(TAG, "Radio firmware does not answer RSSI requests, stopping sweep");
                enter_standby();
                return false;
            }
        }

        spectrum_end_sweep();
        uint32_t sweep_ms = (esp_timer_get_time() - sweep_start) / 1000;
        taskENTER_CRITICAL(&info_lock);
        info.last_sweep_ms = sweep_ms;
        taskEXIT_CRITICAL(&info_lock);
        discard_packets();
        refresh_device_errors();
        telemetry_sweep(params, sweep_ms);
    }
    return true;
}

static lora_protocol_config_params_t channel_config(const listen_channel_t* channel) {
    lora_protocol_config_params_t config = base_config();
    config.frequency                     = channel->frequency_hz;
    config.spreading_factor              = channel->spreading_factor;
    config.bandwidth                     = channel->bandwidth_khz;
    config.coding_rate                   = channel->coding_rate;
    config.sync_word                     = channel->sync_word;
    config.preamble_length               = channel->preamble_length;
    config.invert_iq                     = channel->invert_iq;
    uint32_t symbol_us                   = (1000u << channel->spreading_factor) / channel->bandwidth_khz;
    config.low_data_rate_optimization    = symbol_us >= 16000;
    return config;
}

static void drain_packets(const listen_channel_t* channel, TickType_t wait) {
    lora_protocol_lora_packet_t packet;
    while (lora_receive_packet(&lora, &packet, wait) == ESP_OK) {
        capture_add(&packet, channel);
        wait = 0;
    }
}

static bool tune_listen_channel(const listen_params_t* params, uint8_t index) {
    const listen_channel_t*       channel = &params->channels[index];
    lora_protocol_config_params_t config  = channel_config(channel);

    esp_err_t res = lora_set_config(&lora, &config);
    if (res != ESP_OK) {
        count_rpc_error(res, "set listen config");
        return false;
    }
    res = lora_set_mode(&lora, LORA_PROTOCOL_MODE_RX);
    if (res != ESP_OK) {
        count_rpc_error(res, "set listen rx");
        return false;
    }
    taskENTER_CRITICAL(&info_lock);
    info.listen_channel_index = index;
    info.listen_frequency_hz  = channel->frequency_hz;
    taskEXIT_CRITICAL(&info_lock);
    return true;
}

static void run_listen(const listen_params_t* params, uint32_t generation) {
    if (params->channel_count == 0 || request_changed(generation)) {
        return;
    }
    uint8_t index = 0;
    enter_standby();
    vTaskDelay(pdMS_TO_TICKS(RETUNE_DRAIN_MS));
    discard_packets();
    while (!tune_listen_channel(params, index)) {
        if (request_changed(generation)) {
            return;
        }
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
    int64_t hop_deadline = esp_timer_get_time() + (int64_t)params->dwell_ms * 1000;

    while (!request_changed(generation)) {
        lora_protocol_lora_packet_t packet;
        if (lora_receive_packet(&lora, &packet, pdMS_TO_TICKS(50)) == ESP_OK) {
            capture_add(&packet, &params->channels[index]);
        }
        if (params->channel_count > 1 && esp_timer_get_time() >= hop_deadline) {
            enter_standby();
            drain_packets(&params->channels[index], pdMS_TO_TICKS(RETUNE_DRAIN_MS));
            index = (index + 1) % params->channel_count;
            bool tuned   = tune_listen_channel(params, index);
            hop_deadline = esp_timer_get_time() + (tuned ? (int64_t)params->dwell_ms * 1000 : 1000000);
        }
    }
    enter_standby();
    drain_packets(&params->channels[index], pdMS_TO_TICKS(RETUNE_DRAIN_MS));
}

static bool config_matches_original(void) {
    lora_protocol_config_params_t current = {0};
    return lora_get_config(&lora, &current) == ESP_OK &&
           memcmp(&current, &original_config, sizeof(current)) == 0;
}

static void restore_original(void) {
    bool restored = false;
    for (int attempt = 0; attempt < RESTORE_ATTEMPTS && !restored; attempt++) {
        enter_standby();
        lora_set_config(&lora, &original_config);
        restored = config_matches_original();
    }
    if (!restored) {
        ESP_LOGE(TAG, "Failed to restore the radio's previous LoRa config");
        return;
    }
    if (original_offset_known && lora_set_frequency_offset(&lora, original_offset_hz) != ESP_OK) {
        ESP_LOGW(TAG, "Failed to restore the frequency offset");
    }
    if (original_mode == LORA_PROTOCOL_MODE_RX || original_mode == LORA_PROTOCOL_MODE_STANDBY_XOSC) {
        lora_set_mode(&lora, original_mode);
    }
    ESP_LOGI(TAG, "Restored previous LoRa config");
}

static void radio_task(void* arg) {
    static sweep_params_t  sweep;
    static listen_params_t listen;
    bool                   standing_by = false;

    while (1) {
        xSemaphoreTake(request_mutex, portMAX_DELAY);
        activity_t activity   = requested_activity;
        uint32_t   generation = request_generation;
        if (activity == ACTIVITY_SWEEP) {
            sweep = requested_sweep;
        } else if (activity == ACTIVITY_LISTEN) {
            listen = requested_listen;
        }
        xSemaphoreGive(request_mutex);

        switch (activity) {
            case ACTIVITY_SWEEP:
                xEventGroupClearBits(radio_events, IDLE_BIT);
                standing_by = false;
                if (!run_sweep(&sweep, generation)) {
                    while (!request_changed(generation)) {
                        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
                    }
                }
                break;
            case ACTIVITY_LISTEN:
                xEventGroupClearBits(radio_events, IDLE_BIT);
                standing_by = false;
                run_listen(&listen, generation);
                break;
            case ACTIVITY_IDLE:
            case ACTIVITY_STOPPED:
            default:
                if (!standing_by && activity == ACTIVITY_IDLE) {
                    enter_standby();
                    standing_by = true;
                }
                xEventGroupSetBits(radio_events, IDLE_BIT);
                if (activity == ACTIVITY_STOPPED) {
                    restore_original();
                    xEventGroupSetBits(radio_events, STOPPED_BIT);
                    vTaskSuspend(NULL);
                }
                if (!request_changed(generation)) {
                    ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
                }
                break;
        }
    }
}

static void submit_request(activity_t activity, const sweep_params_t* sweep, const listen_params_t* listen) {
    xSemaphoreTake(request_mutex, portMAX_DELAY);
    if (requested_activity != ACTIVITY_STOPPED) {
        requested_activity = activity;
        if (sweep) {
            requested_sweep = *sweep;
        }
        if (listen) {
            requested_listen = *listen;
        }
        request_generation++;
    }
    xSemaphoreGive(request_mutex);
    if (radio_task_handle) {
        xTaskNotifyGive(radio_task_handle);
    }
}

esp_err_t radio_start(void) {
    request_mutex = xSemaphoreCreateMutex();
    radio_events  = xEventGroupCreate();
    if (!request_mutex || !radio_events) {
        return ESP_ERR_NO_MEM;
    }

    esp_err_t res = lora_init_remote(&lora, PACKET_QUEUE_LENGTH);
    if (res != ESP_OK) {
        ESP_LOGE(TAG, "LoRa init failed: %s", esp_err_to_name(res));
        return res;
    }

    lora_protocol_status_params_t status = {0};
    res                                  = lora_get_status(&lora, &status);
    if (res != ESP_OK) {
        ESP_LOGE(TAG, "LoRa radio not responding: %s", esp_err_to_name(res));
        return res;
    }
    res = lora_get_config(&lora, &original_config);
    if (res != ESP_OK) {
        ESP_LOGE(TAG, "Reading LoRa config failed: %s", esp_err_to_name(res));
        return res;
    }
    if (lora_get_mode(&lora, &original_mode) != ESP_OK) {
        original_mode = LORA_PROTOCOL_MODE_UNKNOWN;
    }
    original_offset_known = lora_get_frequency_offset(&lora, NULL, NULL, &original_offset_hz) == ESP_OK;

    taskENTER_CRITICAL(&info_lock);
    info.available     = true;
    info.chip_type     = status.chip_type;
    info.device_errors = status.errors;
    memcpy(info.version, status.version_string, LORA_PROTOCOL_VERSION_STRING_LENGTH);
    info.version[LORA_PROTOCOL_VERSION_STRING_LENGTH] = '\0';
    taskEXIT_CRITICAL(&info_lock);

    ESP_LOGI(TAG, "Radio %s, saved config %lu Hz SF%u BW%u sync 0x%02x mode %d", info.version,
             (unsigned long)original_config.frequency, original_config.spreading_factor, original_config.bandwidth,
             original_config.sync_word, original_mode);

    if (xTaskCreate(radio_task, "radio", 8192, NULL, 5, &radio_task_handle) != pdPASS) {
        return ESP_ERR_NO_MEM;
    }
    return ESP_OK;
}

void radio_request_idle(void) {
    submit_request(ACTIVITY_IDLE, NULL, NULL);
}

void radio_request_sweep(const sweep_params_t* params) {
    submit_request(ACTIVITY_SWEEP, params, NULL);
}

void radio_request_listen(const listen_params_t* params) {
    submit_request(ACTIVITY_LISTEN, NULL, params);
}

void radio_get_info(radio_info_t* out_info) {
    taskENTER_CRITICAL(&info_lock);
    *out_info = info;
    taskEXIT_CRITICAL(&info_lock);
}

const char* radio_sweep_method_name(sweep_method_t method) {
    switch (method) {
        case SWEEP_RETUNE_ONLY:
            return "retune";
        case SWEEP_RETUNE_THEN_RX:
            return "retune+RX";
        case SWEEP_STANDBY_RETUNE_RX:
            return "standby+retune+RX";
        default:
            return "?";
    }
}

void radio_restore_and_stop(void) {
    if (!radio_task_handle) {
        return;
    }
    submit_request(ACTIVITY_STOPPED, NULL, NULL);
    if (!(xEventGroupWaitBits(radio_events, STOPPED_BIT, pdFALSE, pdTRUE, pdMS_TO_TICKS(20000)) & STOPPED_BIT)) {
        ESP_LOGE(TAG, "Radio task did not finish restoring the radio in time");
    }
}
