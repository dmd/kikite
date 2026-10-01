#pragma once

#include <stdbool.h>
#include <stdint.h>

#define DECODE_MAX_DETAILS    14
#define DECODE_DETAIL_LENGTH  64
#define DECODE_SUMMARY_LENGTH 64
#define DECODE_LABEL_LENGTH   32

typedef enum {
    PROTOCOL_UNKNOWN,
    PROTOCOL_LORAWAN,
    PROTOCOL_MESHTASTIC,
    PROTOCOL_MESHCORE,
} protocol_t;

typedef struct {
    protocol_t protocol;
    bool       has_source;
    uint64_t   source_id;
    char       source_text[24];
    char       source_label[DECODE_LABEL_LENGTH];
    char       summary[DECODE_SUMMARY_LENGTH];
    uint8_t    detail_count;
    char       details[DECODE_MAX_DETAILS][DECODE_DETAIL_LENGTH];
} decoded_packet_t;

const char* decode_protocol_name(protocol_t protocol);
void        decode_packet(const uint8_t* data, uint8_t length, uint8_t sync_word, bool invert_iq,
                          decoded_packet_t* out);
