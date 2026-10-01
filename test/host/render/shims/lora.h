#pragma once
#include <stdbool.h>
#include <stdint.h>
#define LORA_PROTOCOL_VERSION_STRING_LENGTH 16
typedef enum { LORA_PROTOCOL_CHIP_SX1262 = 0x00, LORA_PROTOCOL_CHIP_SX1268 = 0x01 } lora_protocol_chip_t;
typedef struct {
    uint8_t rssi_pkt_raw;
    int8_t  snr_pkt_raw;
    uint8_t signal_rssi_pkt_raw;
} __attribute__((packed)) lora_packet_stats_t;
typedef struct {
    lora_packet_stats_t stats;
    uint8_t             length;
    uint8_t             data[256];
} __attribute__((packed)) lora_protocol_lora_packet_t;
