#include "decode.h"
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include "lorawan_netid.h"
#include "meshtastic.h"

#define SYNC_LORAWAN    0x34
#define SYNC_MESHTASTIC 0x2B
#define SYNC_PRIVATE    0x12

static uint32_t read_u32_le(const uint8_t* bytes) {
    return (uint32_t)bytes[0] | (uint32_t)bytes[1] << 8 | (uint32_t)bytes[2] << 16 | (uint32_t)bytes[3] << 24;
}

static uint16_t read_u16_le(const uint8_t* bytes) {
    return (uint16_t)(bytes[0] | bytes[1] << 8);
}

static uint64_t read_eui_le(const uint8_t* bytes) {
    uint64_t value = 0;
    for (int i = 7; i >= 0; i--) {
        value = value << 8 | bytes[i];
    }
    return value;
}

static void add_detail(decoded_packet_t* out, const char* fmt, ...) {
    if (out->detail_count >= DECODE_MAX_DETAILS) {
        return;
    }
    va_list args;
    va_start(args, fmt);
    vsnprintf(out->details[out->detail_count++], DECODE_DETAIL_LENGTH, fmt, args);
    va_end(args);
}

static void summarize(decoded_packet_t* out, const char* fmt, ...) {
    va_list args;
    va_start(args, fmt);
    vsnprintf(out->summary, DECODE_SUMMARY_LENGTH, fmt, args);
    va_end(args);
}

const char* decode_protocol_name(protocol_t protocol) {
    switch (protocol) {
        case PROTOCOL_LORAWAN:
            return "LoRaWAN";
        case PROTOCOL_MESHTASTIC:
            return "Meshtast";
        case PROTOCOL_MESHCORE:
            return "MeshCore";
        default:
            return "LoRa";
    }
}

static const char* const lorawan_types[8] = {
    "JoinRequest", "JoinAccept", "UnconfUp", "UnconfDown", "ConfUp", "ConfDown", "RFU", "Proprietary",
};

static void decode_lorawan_data(const uint8_t* data, uint8_t length, uint8_t mtype, decoded_packet_t* out) {
    bool uplink = mtype == 2 || mtype == 4;
    if (length < 12) {
        summarize(out, "%s, truncated", lorawan_types[mtype]);
        return;
    }
    uint32_t dev_addr     = read_u32_le(&data[1]);
    uint8_t  fctrl        = data[5];
    uint16_t fcnt         = read_u16_le(&data[6]);
    uint8_t  fopts_length = fctrl & 0x0F;
    int      after_fhdr   = 8 + fopts_length;
    int      remaining    = length - after_fhdr - 4;
    if (remaining < 0) {
        summarize(out, "%s, bad FOpts length", lorawan_types[mtype]);
        return;
    }

    lorawan_netid_t netid;
    bool            netid_valid = lorawan_netid_from_devaddr(dev_addr, &netid);
    const char*     network_operator    = netid_valid ? lorawan_network_operator_name(netid.netid) : NULL;

    out->has_source = true;
    out->source_id  = dev_addr;
    snprintf(out->source_text, sizeof(out->source_text), "%08lX", (unsigned long)dev_addr);
    if (network_operator) {
        snprintf(out->source_label, sizeof(out->source_label), "%s", network_operator);
    } else if (netid_valid) {
        snprintf(out->source_label, sizeof(out->source_label), "NetID %06lX", (unsigned long)netid.netid);
    }

    add_detail(out, "Type        %s (%s)", lorawan_types[mtype], uplink ? "device to network" : "network to device");
    add_detail(out, "DevAddr     %08lX", (unsigned long)dev_addr);
    if (netid_valid) {
        add_detail(out, "Network     NetID %06lX type %u%s%s", (unsigned long)netid.netid, netid.type,
                   network_operator ? ", " : "", network_operator ? network_operator : "");
    } else {
        add_detail(out, "Network     invalid DevAddr prefix");
    }
    add_detail(out, "Frame count %u", fcnt);
    if (uplink) {
        add_detail(out, "Flags       %s%s%s%s", fctrl & 0x80 ? "ADR " : "", fctrl & 0x40 ? "ADRACKReq " : "",
                   fctrl & 0x20 ? "ACK " : "", fctrl & 0x10 ? "ClassB " : "");
    } else {
        add_detail(out, "Flags       %s%s%s", fctrl & 0x80 ? "ADR " : "", fctrl & 0x20 ? "ACK " : "",
                   fctrl & 0x10 ? "FPending " : "");
    }
    if (fopts_length) {
        add_detail(out, "MAC options %u bytes (unencrypted)", fopts_length);
    }

    if (remaining > 0) {
        uint8_t port = data[after_fhdr];
        add_detail(out, "Port        %u%s", port, port == 0 ? " (MAC commands)" : port == 224 ? " (test)" : "");
        add_detail(out, "Payload     %d bytes, encrypted", remaining - 1);
        summarize(out, "%s %08lX #%u p%u %s", uplink ? "up" : "down", (unsigned long)dev_addr, fcnt, port,
                  out->source_label);
    } else {
        summarize(out, "%s %08lX #%u %s", uplink ? "up" : "down", (unsigned long)dev_addr, fcnt, out->source_label);
    }
}

static void decode_lorawan(const uint8_t* data, uint8_t length, decoded_packet_t* out) {
    out->protocol = PROTOCOL_LORAWAN;
    if (length < 5) {
        summarize(out, "too short for LoRaWAN");
        return;
    }
    uint8_t mtype = data[0] >> 5;
    uint8_t major = data[0] & 0x03;
    if (major != 0 && mtype != 7) {
        summarize(out, "unknown LoRaWAN major version %u", major);
        return;
    }
    switch (mtype) {
        case 0: {
            if (length != 23) {
                summarize(out, "JoinRequest, wrong length %u", length);
                return;
            }
            uint64_t join_eui  = read_eui_le(&data[1]);
            uint64_t dev_eui   = read_eui_le(&data[9]);
            uint16_t dev_nonce = read_u16_le(&data[17]);
            out->has_source    = true;
            out->source_id     = dev_eui;
            snprintf(out->source_text, sizeof(out->source_text), "%016llX", (unsigned long long)dev_eui);
            snprintf(out->source_label, sizeof(out->source_label), "joining");
            add_detail(out, "Type        JoinRequest (device joining)");
            add_detail(out, "DevEUI      %016llX", (unsigned long long)dev_eui);
            add_detail(out, "JoinEUI     %016llX", (unsigned long long)join_eui);
            add_detail(out, "DevNonce    %u", dev_nonce);
            summarize(out, "join %016llX", (unsigned long long)dev_eui);
            return;
        }
        case 1:
            add_detail(out, "Type        JoinAccept (encrypted)");
            summarize(out, "join accept (encrypted)");
            return;
        case 2:
        case 3:
        case 4:
        case 5:
            decode_lorawan_data(data, length, mtype, out);
            return;
        case 7:
            add_detail(out, "Type        Proprietary");
            summarize(out, "proprietary frame");
            return;
        default:
            summarize(out, "reserved message type");
            return;
    }
}

static void decode_meshtastic(const uint8_t* data, uint8_t length, decoded_packet_t* out) {
    out->protocol = PROTOCOL_MESHTASTIC;
    if (length < 16) {
        summarize(out, "too short for Meshtastic");
        return;
    }
    uint32_t to         = read_u32_le(&data[0]);
    uint32_t from       = read_u32_le(&data[4]);
    uint32_t id         = read_u32_le(&data[8]);
    uint8_t  flags      = data[12];
    uint8_t  hash       = data[13];
    uint8_t  next_hop   = data[14];
    uint8_t  relay_node = data[15];
    uint8_t  hop_limit  = flags & 0x07;
    uint8_t  hop_start  = (flags & 0xE0) >> 5;

    out->has_source = true;
    out->source_id  = from;
    snprintf(out->source_text, sizeof(out->source_text), "!%08lx", (unsigned long)from);

    char destination[16];
    if (to == 0xFFFFFFFF) {
        snprintf(destination, sizeof(destination), "everyone");
    } else {
        snprintf(destination, sizeof(destination), "!%08lx", (unsigned long)to);
    }
    add_detail(out, "From        !%08lx", (unsigned long)from);
    add_detail(out, "To          %s", destination);
    add_detail(out, "Packet ID   %08lX", (unsigned long)id);
    add_detail(out, "Hops        %u left of %u%s%s", hop_limit, hop_start, flags & 0x08 ? ", wants ACK" : "",
               flags & 0x10 ? ", via MQTT" : "");
    const char* preset = meshtastic_default_channel_name(hash);
    if (preset) {
        add_detail(out, "Channel     hash 0x%02X (%s, default key)", hash, preset);
    } else {
        add_detail(out, "Channel     hash 0x%02X (private key)", hash);
    }
    if (relay_node || next_hop) {
        add_detail(out, "Relay       last byte %02X, next hop %02X", relay_node, next_hop);
    }
    char route[24] = "";
    if (hop_start >= hop_limit && hop_start > 0) {
        snprintf(route, sizeof(route), " hop %d/%u", hop_start - hop_limit, hop_start);
    }
    const char* to_text = to == 0xFFFFFFFF ? "all" : destination;

    uint8_t           plain[256];
    size_t            plain_length = 0;
    meshtastic_data_t payload;
    if (!meshtastic_decrypt_default(data, length, plain, &plain_length) ||
        !meshtastic_parse_data(plain, plain_length, &payload)) {
        add_detail(out, "Payload     %u bytes, encrypted", length - 16);
        summarize(out, "!%08lx>%s%s", (unsigned long)from, to_text, route);
        return;
    }

    add_detail(out, "App         %s (port %lu)", meshtastic_port_name(payload.portnum),
               (unsigned long)payload.portnum);
    if (payload.has_text) {
        size_t text_length = strlen(payload.text);
        for (size_t offset = 0; offset < text_length && out->detail_count < DECODE_MAX_DETAILS; offset += 44) {
            add_detail(out, "%s%.44s", offset == 0 ? "Text        " : "            ", payload.text + offset);
        }
        summarize(out, "!%08lx: %s", (unsigned long)from, payload.text);
    } else if (payload.has_user) {
        add_detail(out, "Name        %s (%s)", payload.long_name, payload.short_name);
        add_detail(out, "User ID     %s, hardware model %lu", payload.user_id, (unsigned long)payload.hw_model);
        snprintf(out->source_label, sizeof(out->source_label), "%.31s", payload.long_name);
        summarize(out, "!%08lx is %s", (unsigned long)from, payload.long_name);
    } else if (payload.has_position) {
        add_detail(out, "Location    %.5f, %.5f", payload.latitude_i / 1e7, payload.longitude_i / 1e7);
        if (payload.has_altitude) {
            add_detail(out, "Altitude    %ld m", (long)payload.altitude);
        }
        summarize(out, "!%08lx at %.3f,%.3f", (unsigned long)from, payload.latitude_i / 1e7,
                  payload.longitude_i / 1e7);
    } else {
        summarize(out, "!%08lx %s", (unsigned long)from, meshtastic_port_name(payload.portnum));
    }
}

static const char* const meshcore_types[16] = {
    "request", "response", "text", "ack", "advert", "group text", "group data", "anon request",
    "path", "trace", "multipart", "control", "type 12", "type 13", "type 14", "raw custom",
};

static const char* const meshcore_routes[4] = {"transport flood", "flood", "direct", "transport direct"};

static const char* const meshcore_node_types[5] = {"node", "chat", "repeater", "room", "sensor"};

static void decode_meshcore_advert(const uint8_t* payload, int payload_length, decoded_packet_t* out) {
    if (payload_length < 32 + 4 + 64 + 1) {
        summarize(out, "advert, truncated");
        return;
    }
    const uint8_t* public_key = payload;
    uint32_t       timestamp  = read_u32_le(&payload[32]);
    const uint8_t* app_data   = &payload[100];
    int            app_length = payload_length - 100;
    uint8_t        flags      = app_data[0];
    int            offset     = 1;
    uint8_t        node_type  = flags & 0x0F;

    out->has_source = true;
    out->source_id  = (uint64_t)read_u32_le(public_key) << 16 | read_u16_le(&public_key[4]);
    snprintf(out->source_text, sizeof(out->source_text), "%02X%02X%02X%02X%02X%02X", public_key[0], public_key[1],
             public_key[2], public_key[3], public_key[4], public_key[5]);
    add_detail(out, "Key         %02X%02X%02X%02X%02X%02X%02X%02X...", public_key[0], public_key[1], public_key[2],
               public_key[3], public_key[4], public_key[5], public_key[6], public_key[7]);
    add_detail(out, "Node type   %s", node_type < 5 ? meshcore_node_types[node_type] : "unknown");
    add_detail(out, "Timestamp   %lu", (unsigned long)timestamp);

    if (flags & 0x10) {
        if (offset + 8 > app_length) {
            summarize(out, "advert, truncated location");
            return;
        }
        int32_t latitude  = (int32_t)read_u32_le(&app_data[offset]);
        int32_t longitude = (int32_t)read_u32_le(&app_data[offset + 4]);
        offset += 8;
        add_detail(out, "Location    %.5f, %.5f", latitude / 1e6, longitude / 1e6);
    }
    if (flags & 0x20) {
        offset += 2;
    }
    if (flags & 0x40) {
        offset += 2;
    }
    char name[DECODE_LABEL_LENGTH] = "";
    if ((flags & 0x80) && offset < app_length) {
        int name_length = app_length - offset;
        if (name_length >= (int)sizeof(name)) {
            name_length = sizeof(name) - 1;
        }
        for (int i = 0; i < name_length; i++) {
            char c  = (char)app_data[offset + i];
            name[i] = (c >= 0x20 && c < 0x7F) ? c : '?';
            if (c == '\0') {
                name[i] = '\0';
                break;
            }
        }
        add_detail(out, "Name        %s", name);
        snprintf(out->source_label, sizeof(out->source_label), "%s", name);
    }
    summarize(out, "advert %s %s", node_type < 5 ? meshcore_node_types[node_type] : "?", name);
}

static void decode_meshcore(const uint8_t* data, uint8_t length, decoded_packet_t* out) {
    if (length < 2) {
        summarize(out, "too short");
        return;
    }
    uint8_t header       = data[0];
    uint8_t route        = header & 0x03;
    uint8_t payload_type = (header >> 2) & 0x0F;
    uint8_t version      = header >> 6;
    int     offset       = 1;
    if (version != 0) {
        return;
    }
    if (route == 0 || route == 3) {
        offset += 4;
    }
    if (offset >= length) {
        return;
    }
    uint8_t path_byte = data[offset++];
    uint8_t hops      = path_byte & 0x3F;
    uint8_t hash_size = (path_byte >> 6) + 1;
    int     path_size = hops * hash_size;
    if (hops > 64 || offset + path_size > length) {
        return;
    }
    const uint8_t* payload        = &data[offset + path_size];
    int            payload_length = length - offset - path_size;

    out->protocol = PROTOCOL_MESHCORE;
    add_detail(out, "Type        %s", meshcore_types[payload_type]);
    add_detail(out, "Route       %s, %u hop%s", meshcore_routes[route], hops, hops == 1 ? "" : "s");

    switch (payload_type) {
        case 4:
            decode_meshcore_advert(payload, payload_length, out);
            return;
        case 0:
        case 1:
        case 2:
        case 8:
            if (payload_length >= 2) {
                add_detail(out, "Addresses   to %02X from %02X (key hash bytes)", payload[0], payload[1]);
                add_detail(out, "Payload     %d bytes, encrypted", payload_length - 2);
                summarize(out, "%s %02X>%02X %uh", meshcore_types[payload_type], payload[1], payload[0], hops);
                return;
            }
            break;
        case 5:
        case 6:
            if (payload_length >= 1) {
                add_detail(out, "Channel     hash %02X", payload[0]);
                add_detail(out, "Payload     %d bytes, encrypted", payload_length - 1);
                summarize(out, "%s ch %02X %uh", meshcore_types[payload_type], payload[0], hops);
                return;
            }
            break;
        default:
            break;
    }
    add_detail(out, "Payload     %d bytes", payload_length);
    summarize(out, "%s %uh", meshcore_types[payload_type], hops);
}

static void decode_unknown(const uint8_t* data, uint8_t length, decoded_packet_t* out) {
    char hex[DECODE_SUMMARY_LENGTH] = "";
    int  used                       = 0;
    for (int i = 0; i < length && i < 12; i++) {
        used += snprintf(hex + used, sizeof(hex) - used, "%02X", data[i]);
    }
    summarize(out, "%s%s", hex, length > 12 ? "..." : "");
}

void decode_packet(const uint8_t* data, uint8_t length, uint8_t sync_word, bool invert_iq, decoded_packet_t* out) {
    memset(out, 0, sizeof(*out));
    out->protocol = PROTOCOL_UNKNOWN;
    switch (sync_word) {
        case SYNC_LORAWAN:
            decode_lorawan(data, length, out);
            break;
        case SYNC_MESHTASTIC:
            decode_meshtastic(data, length, out);
            break;
        case SYNC_PRIVATE:
            decode_meshcore(data, length, out);
            break;
        default:
            break;
    }
    if (out->protocol == PROTOCOL_UNKNOWN && out->summary[0] == '\0') {
        decode_unknown(data, length, out);
    }
}
