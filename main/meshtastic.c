#include "meshtastic.h"
#include <string.h>
#include "aes_ctr.h"

static const uint8_t default_key[16] = {0xd4, 0xf1, 0xbb, 0x3a, 0x20, 0x29, 0x07, 0x59,
                                        0xf0, 0xbc, 0xff, 0xab, 0xcf, 0x4e, 0x69, 0x01};

enum {
    WIRE_VARINT  = 0,
    WIRE_FIXED64 = 1,
    WIRE_BYTES   = 2,
    WIRE_FIXED32 = 5,
};

typedef struct {
    const uint8_t* position;
    const uint8_t* end;
} reader_t;

typedef struct {
    uint32_t       number;
    uint8_t        wire_type;
    uint64_t       value;
    const uint8_t* bytes;
    size_t         length;
} field_t;

static const char* const preset_channel_names[] = {
    "LongFast", "MediumFast", "ShortFast", "LongSlow", "MediumSlow", "ShortSlow", "LongModerate", "ShortTurbo", "LongTurbo",
};

uint8_t meshtastic_channel_hash(const char* name) {
    uint8_t hash = 0;
    for (const char* c = name; *c; c++) {
        hash ^= (uint8_t)*c;
    }
    for (size_t i = 0; i < sizeof(default_key); i++) {
        hash ^= default_key[i];
    }
    return hash;
}

const char* meshtastic_default_channel_name(uint8_t hash) {
    for (size_t i = 0; i < sizeof(preset_channel_names) / sizeof(preset_channel_names[0]); i++) {
        if (meshtastic_channel_hash(preset_channel_names[i]) == hash) {
            return preset_channel_names[i];
        }
    }
    return NULL;
}

bool meshtastic_decrypt_default(const uint8_t* packet, size_t length, uint8_t* plain, size_t* plain_length) {
    if (length <= MESHTASTIC_HEADER_LENGTH || !meshtastic_default_channel_name(packet[13])) {
        return false;
    }
    uint8_t nonce[16] = {0};
    memcpy(&nonce[0], &packet[8], 4);
    memcpy(&nonce[8], &packet[4], 4);
    *plain_length = length - MESHTASTIC_HEADER_LENGTH;
    return aes128_ctr(default_key, nonce, packet + MESHTASTIC_HEADER_LENGTH, plain, *plain_length);
}

static bool read_varint(reader_t* reader, uint64_t* out) {
    uint64_t value = 0;
    for (int shift = 0; shift < 64; shift += 7) {
        if (reader->position >= reader->end) {
            return false;
        }
        uint8_t byte = *reader->position++;
        value |= (uint64_t)(byte & 0x7F) << shift;
        if (!(byte & 0x80)) {
            *out = value;
            return true;
        }
    }
    return false;
}

static bool next_field(reader_t* reader, field_t* field) {
    uint64_t key;
    if (!read_varint(reader, &key)) {
        return false;
    }
    field->number    = (uint32_t)(key >> 3);
    field->wire_type = key & 0x07;
    if (field->number == 0 || key >> 3 > 536870911u) {
        return false;
    }
    switch (field->wire_type) {
        case WIRE_VARINT:
            return read_varint(reader, &field->value);
        case WIRE_FIXED64:
            if (reader->end - reader->position < 8) {
                return false;
            }
            field->value = 0;
            for (int i = 7; i >= 0; i--) {
                field->value = field->value << 8 | reader->position[i];
            }
            reader->position += 8;
            return true;
        case WIRE_FIXED32:
            if (reader->end - reader->position < 4) {
                return false;
            }
            field->value = (uint32_t)reader->position[0] | (uint32_t)reader->position[1] << 8 |
                           (uint32_t)reader->position[2] << 16 | (uint32_t)reader->position[3] << 24;
            reader->position += 4;
            return true;
        case WIRE_BYTES: {
            uint64_t length;
            if (!read_varint(reader, &length) || length > (uint64_t)(reader->end - reader->position)) {
                return false;
            }
            field->bytes  = reader->position;
            field->length = (size_t)length;
            reader->position += length;
            return true;
        }
        default:
            return false;
    }
}

static void copy_printable(char* out, size_t size, const uint8_t* bytes, size_t length) {
    size_t used = 0;
    for (size_t i = 0; i < length && used + 1 < size; i++) {
        uint8_t byte = bytes[i];
        if (byte >= 0x20 && byte < 0x7F) {
            out[used++] = (char)byte;
        } else if (byte == '\n' || byte == '\t') {
            out[used++] = ' ';
        } else if (byte >= 0xC0) {
            out[used++] = '?';
        }
    }
    out[used] = '\0';
}

static void parse_user(const uint8_t* bytes, size_t length, meshtastic_data_t* out) {
    reader_t reader = {bytes, bytes + length};
    field_t  field;
    while (reader.position < reader.end && next_field(&reader, &field)) {
        if (field.wire_type == WIRE_BYTES && field.number == 1) {
            copy_printable(out->user_id, sizeof(out->user_id), field.bytes, field.length);
        } else if (field.wire_type == WIRE_BYTES && field.number == 2) {
            copy_printable(out->long_name, sizeof(out->long_name), field.bytes, field.length);
        } else if (field.wire_type == WIRE_BYTES && field.number == 3) {
            copy_printable(out->short_name, sizeof(out->short_name), field.bytes, field.length);
        } else if (field.wire_type == WIRE_VARINT && field.number == 5) {
            out->hw_model = (uint32_t)field.value;
        }
    }
    out->has_user = out->long_name[0] != '\0' || out->user_id[0] != '\0';
}

static void parse_position(const uint8_t* bytes, size_t length, meshtastic_data_t* out) {
    reader_t reader       = {bytes, bytes + length};
    field_t  field;
    bool     has_latitude = false, has_longitude = false;
    while (reader.position < reader.end && next_field(&reader, &field)) {
        if (field.wire_type == WIRE_FIXED32 && field.number == 1) {
            out->latitude_i = (int32_t)(uint32_t)field.value;
            has_latitude    = true;
        } else if (field.wire_type == WIRE_FIXED32 && field.number == 2) {
            out->longitude_i = (int32_t)(uint32_t)field.value;
            has_longitude    = true;
        } else if (field.wire_type == WIRE_VARINT && field.number == 3) {
            out->altitude     = (int32_t)(uint32_t)field.value;
            out->has_altitude = true;
        }
    }
    out->has_position = has_latitude && has_longitude;
}

bool meshtastic_parse_data(const uint8_t* bytes, size_t length, meshtastic_data_t* out) {
    memset(out, 0, sizeof(*out));
    reader_t       reader         = {bytes, bytes + length};
    field_t        field;
    bool           has_portnum    = false;
    const uint8_t* payload        = NULL;
    size_t         payload_length = 0;

    while (reader.position < reader.end) {
        if (!next_field(&reader, &field)) {
            return false;
        }
        if (field.number == 1) {
            if (field.wire_type != WIRE_VARINT || field.value == 0 || field.value > 1023) {
                return false;
            }
            out->portnum = (uint32_t)field.value;
            has_portnum  = true;
        } else if (field.number == 2) {
            if (field.wire_type != WIRE_BYTES) {
                return false;
            }
            payload        = field.bytes;
            payload_length = field.length;
        } else if (field.number > 16) {
            return false;
        }
    }
    if (!has_portnum) {
        return false;
    }

    switch (out->portnum) {
        case 1:
            if (payload) {
                copy_printable(out->text, sizeof(out->text), payload, payload_length);
                out->has_text = true;
            }
            break;
        case 3:
            if (payload) {
                parse_position(payload, payload_length, out);
            }
            break;
        case 4:
            if (payload) {
                parse_user(payload, payload_length, out);
            }
            break;
        default:
            break;
    }
    return true;
}

const char* meshtastic_port_name(uint32_t portnum) {
    switch (portnum) {
        case 1:
            return "text message";
        case 3:
            return "position";
        case 4:
            return "node info";
        case 5:
            return "routing";
        case 6:
            return "admin";
        case 7:
            return "compressed text";
        case 8:
            return "waypoint";
        case 10:
            return "detection sensor";
        case 65:
            return "store and forward";
        case 66:
            return "range test";
        case 67:
            return "telemetry";
        case 70:
            return "traceroute";
        case 71:
            return "neighbor info";
        case 73:
            return "map report";
        default:
            return "other app";
    }
}
