#include "capture.h"
#include <string.h>
#include "esp_heap_caps.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "telemetry.h"

static captured_packet_t* packets;
static uint32_t           packet_head;
static uint32_t           packet_count;
static uint32_t           total_received;
static device_t*          devices;
static uint32_t           device_count;
static SemaphoreHandle_t  capture_mutex;
static captured_packet_t* telemetry_copy;

bool capture_init(void) {
    capture_mutex = xSemaphoreCreateMutex();
    packets       = heap_caps_calloc(CAPTURE_MAX_PACKETS, sizeof(captured_packet_t), MALLOC_CAP_SPIRAM);
    devices        = heap_caps_calloc(CAPTURE_MAX_DEVICES, sizeof(device_t), MALLOC_CAP_SPIRAM);
    telemetry_copy = heap_caps_calloc(1, sizeof(captured_packet_t), MALLOC_CAP_SPIRAM);
    return capture_mutex && packets && devices && telemetry_copy;
}

static void record_device(const captured_packet_t* packet) {
    const decoded_packet_t* decoded = &packet->decoded;
    uint32_t                index   = device_count;
    for (uint32_t i = 0; i < device_count; i++) {
        if (devices[i].protocol == decoded->protocol && devices[i].source_id == decoded->source_id) {
            index = i;
            break;
        }
    }

    device_t device;
    if (index < device_count) {
        device = devices[index];
    } else {
        memset(&device, 0, sizeof(device));
        device.protocol      = decoded->protocol;
        device.source_id     = decoded->source_id;
        device.first_seen_us = packet->received_us;
        device.best_rssi_dbm = packet->rssi_dbm;
        if (device_count < CAPTURE_MAX_DEVICES) {
            device_count++;
        }
        index = device_count - 1;
    }

    memcpy(device.source_text, decoded->source_text, sizeof(device.source_text));
    if (decoded->source_label[0] != '\0') {
        memcpy(device.label, decoded->source_label, sizeof(device.label));
    }
    device.packets++;
    device.last_rssi_dbm     = packet->rssi_dbm;
    device.last_snr_db_x4    = packet->snr_db_x4;
    device.last_frequency_hz = packet->channel.frequency_hz;
    device.last_seen_us      = packet->received_us;
    if (packet->rssi_dbm > device.best_rssi_dbm) {
        device.best_rssi_dbm = packet->rssi_dbm;
    }

    memmove(&devices[1], &devices[0], index * sizeof(device_t));
    devices[0] = device;
}

void capture_add(const lora_protocol_lora_packet_t* packet, const listen_channel_t* channel) {
    xSemaphoreTake(capture_mutex, portMAX_DELAY);
    captured_packet_t* slot = &packets[packet_head];
    memset(slot, 0, sizeof(*slot));
    slot->number          = ++total_received;
    slot->received_us     = esp_timer_get_time();
    slot->channel         = *channel;
    slot->rssi_dbm        = packet->stats.snr_pkt_raw;
    slot->signal_rssi_dbm = slot->rssi_dbm;
    slot->snr_db_x4       = CAPTURE_SNR_UNKNOWN;
    slot->raw_stats[0]    = packet->stats.rssi_pkt_raw;
    slot->raw_stats[1]    = (uint8_t)packet->stats.snr_pkt_raw;
    slot->raw_stats[2]    = packet->stats.signal_rssi_pkt_raw;
    slot->length          = packet->length;
    memcpy(slot->data, packet->data, packet->length);
    decode_packet(slot->data, slot->length, channel->sync_word, channel->invert_iq, &slot->decoded);
    if (slot->decoded.has_source) {
        record_device(slot);
    }
    packet_head = (packet_head + 1) % CAPTURE_MAX_PACKETS;
    if (packet_count < CAPTURE_MAX_PACKETS) {
        packet_count++;
    }
    bool report = telemetry_enabled();
    if (report) {
        *telemetry_copy = *slot;
    }
    xSemaphoreGive(capture_mutex);
    if (report) {
        telemetry_packet(telemetry_copy);
    }
}

void capture_clear(void) {
    xSemaphoreTake(capture_mutex, portMAX_DELAY);
    packet_head  = 0;
    packet_count = 0;
    device_count = 0;
    xSemaphoreGive(capture_mutex);
}

void capture_lock(void) {
    xSemaphoreTake(capture_mutex, portMAX_DELAY);
}

void capture_unlock(void) {
    xSemaphoreGive(capture_mutex);
}

uint32_t capture_packet_count(void) {
    return packet_count;
}

uint32_t capture_total_received(void) {
    return total_received;
}

const captured_packet_t* capture_packet(uint32_t age) {
    if (age >= packet_count) {
        return NULL;
    }
    return &packets[(packet_head + CAPTURE_MAX_PACKETS - 1 - age) % CAPTURE_MAX_PACKETS];
}

uint32_t capture_device_count(void) {
    return device_count;
}

const device_t* capture_device_by_recency(uint32_t rank) {
    return rank < device_count ? &devices[rank] : NULL;
}
