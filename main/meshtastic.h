#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define MESHTASTIC_HEADER_LENGTH       16

typedef struct {
    uint32_t portnum;
    bool     has_text;
    char     text[200];
    bool     has_user;
    char     user_id[16];
    char     long_name[40];
    char     short_name[8];
    uint32_t hw_model;
    bool     has_position;
    int32_t  latitude_i;
    int32_t  longitude_i;
    bool     has_altitude;
    int32_t  altitude;
} meshtastic_data_t;

uint8_t     meshtastic_channel_hash(const char* name);
const char* meshtastic_default_channel_name(uint8_t hash);
bool        meshtastic_decrypt_default(const uint8_t* packet, size_t length, uint8_t* plain, size_t* plain_length);
bool        meshtastic_parse_data(const uint8_t* bytes, size_t length, meshtastic_data_t* out);
const char* meshtastic_port_name(uint32_t portnum);
