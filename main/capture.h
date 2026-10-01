#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "decode.h"
#include "lora.h"
#include "radio.h"

#define CAPTURE_MAX_PACKETS 200
#define CAPTURE_MAX_DEVICES 128
#define CAPTURE_SNR_UNKNOWN INT16_MIN

typedef struct {
    uint32_t         number;
    int64_t          received_us;
    listen_channel_t channel;
    int16_t          rssi_dbm;
    int16_t          signal_rssi_dbm;
    int16_t          snr_db_x4;
    uint8_t          raw_stats[3];
    uint8_t          length;
    uint8_t          data[256];
    decoded_packet_t decoded;
} captured_packet_t;

typedef struct {
    protocol_t protocol;
    uint64_t   source_id;
    char       source_text[24];
    char       label[DECODE_LABEL_LENGTH];
    uint32_t   packets;
    int16_t    last_rssi_dbm;
    int16_t    best_rssi_dbm;
    int16_t    last_snr_db_x4;
    uint32_t   last_frequency_hz;
    int64_t    first_seen_us;
    int64_t    last_seen_us;
} device_t;

bool                     capture_init(void);
void                     capture_add(const lora_protocol_lora_packet_t* packet, const listen_channel_t* channel);
void                     capture_clear(void);
void                     capture_lock(void);
void                     capture_unlock(void);
uint32_t                 capture_packet_count(void);
uint32_t                 capture_total_received(void);
const captured_packet_t* capture_packet(uint32_t age);
uint32_t                 capture_device_count(void);
const device_t*          capture_device_by_recency(uint32_t rank);
