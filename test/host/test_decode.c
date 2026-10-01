#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "decode.h"
#include "lorawan_netid.h"
#include "aes_ctr.h"
#include "meshtastic.h"
#include "meshtastic_slot.h"

static int failures = 0;

#define CHECK(condition)                                                    \
    do {                                                                    \
        if (!(condition)) {                                                 \
            fprintf(stderr, "%s:%d: FAILED %s\n", __FILE__, __LINE__, #condition); \
            failures++;                                                     \
        }                                                                   \
    } while (0)

static size_t from_hex(const char* hex, uint8_t* out) {
    size_t length = strlen(hex) / 2;
    for (size_t i = 0; i < length; i++) {
        unsigned value;
        sscanf(hex + 2 * i, "%2x", &value);
        out[i] = (uint8_t)value;
    }
    return length;
}

static bool has_detail(const decoded_packet_t* packet, const char* text) {
    for (int i = 0; i < packet->detail_count; i++) {
        if (strstr(packet->details[i], text)) {
            return true;
        }
    }
    return false;
}

static void test_netid(void) {
    lorawan_netid_t netid;
    CHECK(lorawan_netid_from_devaddr(0x26011234, &netid));
    CHECK(netid.type == 0 && netid.netid == 0x000013);
    CHECK(strcmp(lorawan_network_operator_name(netid.netid), "TTN") == 0);

    CHECK(lorawan_netid_from_devaddr(0x78000001, &netid) && netid.netid == 0x00003C);
    CHECK(lorawan_netid_from_devaddr(0xE05A1234, &netid) && netid.type == 3 && netid.netid == 0x60002D);
    CHECK(lorawan_netid_from_devaddr(0xFC014C00, &netid) && netid.type == 6 && netid.netid == 0xC00053);
    CHECK(lorawan_netid_from_devaddr(0xFC014FFF, &netid) && netid.netid == 0xC00053);
    CHECK(lorawan_netid_from_devaddr(0xE0201234, &netid) && netid.netid == 0x600010);
    CHECK(lorawan_netid_from_devaddr(0x48001234, &netid) && netid.netid == 0x000024);
    CHECK(!lorawan_netid_from_devaddr(0xFF000000, &netid));
    CHECK(lorawan_network_operator_name(0x123456) == NULL);
}

static void test_lorawan_data_uplink(void) {
    uint8_t          data[64];
    size_t           length = from_hex("40F17DBE4900020001954378762B11FF0D", data);
    decoded_packet_t packet;
    decode_packet(data, length, 0x34, false, &packet);
    CHECK(packet.protocol == PROTOCOL_LORAWAN);
    CHECK(packet.has_source && packet.source_id == 0x49BE7DF1);
    CHECK(strcmp(packet.source_text, "49BE7DF1") == 0);
    CHECK(has_detail(&packet, "UnconfUp"));
    CHECK(has_detail(&packet, "Frame count 2"));
    CHECK(has_detail(&packet, "Port        1"));
    CHECK(has_detail(&packet, "Payload     4 bytes"));
    CHECK(strstr(packet.summary, "up 49BE7DF1 #2 p1") == packet.summary);
}

static void test_lorawan_downlink_with_fopts(void) {
    uint8_t data[] = {0x60, 0x34, 0x12, 0x01, 0x26, 0xA2, 0x05, 0x00, 0x03, 0x01, 0xAA, 0xBB, 0xCC, 0xDD};
    decoded_packet_t packet;
    decode_packet(data, sizeof(data), 0x34, true, &packet);
    CHECK(packet.protocol == PROTOCOL_LORAWAN);
    CHECK(packet.source_id == 0x26011234);
    CHECK(strcmp(packet.source_label, "TTN") == 0);
    CHECK(has_detail(&packet, "UnconfDown"));
    CHECK(has_detail(&packet, "ADR ACK"));
    CHECK(has_detail(&packet, "MAC options 2 bytes"));
    CHECK(has_detail(&packet, "Frame count 5"));
    CHECK(!has_detail(&packet, "Port"));
}

static void test_lorawan_join_request(void) {
    uint8_t data[23] = {0x00};
    for (int i = 0; i < 8; i++) {
        data[1 + i] = 0x10 + i;
        data[9 + i] = 0xA0 + i;
    }
    data[17] = 0x34;
    data[18] = 0x12;
    decoded_packet_t packet;
    decode_packet(data, sizeof(data), 0x34, false, &packet);
    CHECK(packet.has_source);
    CHECK(packet.source_id == 0xA7A6A5A4A3A2A1A0ull);
    CHECK(strcmp(packet.source_text, "A7A6A5A4A3A2A1A0") == 0);
    CHECK(has_detail(&packet, "JoinEUI     1716151413121110"));
    CHECK(has_detail(&packet, "DevNonce    4660"));
}

static void test_lorawan_bad_lengths(void) {
    uint8_t          data[32] = {0x40, 1, 2, 3, 4, 0x0F};
    decoded_packet_t packet;
    decode_packet(data, 13, 0x34, false, &packet);
    CHECK(!packet.has_source);
    CHECK(strstr(packet.summary, "bad FOpts") != NULL);
    decode_packet(data, 3, 0x34, false, &packet);
    CHECK(!packet.has_source);
    data[0] = 0x00;
    decode_packet(data, 20, 0x34, false, &packet);
    CHECK(!packet.has_source);
}

static void test_meshtastic(void) {
    uint8_t data[24] = {
        0xFF, 0xFF, 0xFF, 0xFF, 0x78, 0x56, 0x34, 0x12, 0x01, 0x00, 0x00, 0x00, 0x63, 0x08, 0x00, 0x9A,
    };
    decoded_packet_t packet;
    decode_packet(data, sizeof(data), 0x2B, false, &packet);
    CHECK(packet.protocol == PROTOCOL_MESHTASTIC);
    CHECK(packet.source_id == 0x12345678);
    CHECK(strcmp(packet.source_text, "!12345678") == 0);
    CHECK(has_detail(&packet, "To          everyone"));
    CHECK(has_detail(&packet, "Hops        3 left of 3"));
    CHECK(has_detail(&packet, "hash 0x08 (LongFast, default key)"));
    CHECK(has_detail(&packet, "Payload     8 bytes"));
    CHECK(strcmp(packet.summary, "!12345678>all hop 0/3") == 0);

    data[12] = 0x01;
    decode_packet(data, sizeof(data), 0x2B, false, &packet);
    CHECK(strcmp(packet.summary, "!12345678>all") == 0);
}

static void test_meshcore_advert(void) {
    uint8_t data[160] = {0};
    int     length    = 0;
    data[length++]    = 0x11;
    data[length++]    = 0x00;
    for (int i = 0; i < 32; i++) {
        data[length++] = 0xC0 + i;
    }
    data[length++] = 0x10;
    data[length++] = 0x20;
    data[length++] = 0x30;
    data[length++] = 0x40;
    length += 64;
    data[length++]   = 0x92;
    int32_t latitude = 42387000, longitude = -71099000;
    memcpy(&data[length], &latitude, 4);
    memcpy(&data[length + 4], &longitude, 4);
    length += 8;
    memcpy(&data[length], "Spring St", 9);
    length += 9;

    decoded_packet_t packet;
    decode_packet(data, length, 0x12, false, &packet);
    CHECK(packet.protocol == PROTOCOL_MESHCORE);
    CHECK(packet.has_source);
    CHECK(strcmp(packet.source_text, "C0C1C2C3C4C5") == 0);
    CHECK(strcmp(packet.source_label, "Spring St") == 0);
    CHECK(has_detail(&packet, "Node type   repeater"));
    CHECK(has_detail(&packet, "Location    42.38700, -71.09900"));
    CHECK(has_detail(&packet, "Route       flood, 0 hops"));
    CHECK(strcmp(packet.summary, "advert repeater Spring St") == 0);
}

static void test_meshcore_text_and_garbage(void) {
    uint8_t          data[] = {0x0A, 0x02, 0xAA, 0xBB, 0x5D, 0x7E, 0x01, 0x02, 0x03, 0x04};
    decoded_packet_t packet;
    decode_packet(data, sizeof(data), 0x12, false, &packet);
    CHECK(packet.protocol == PROTOCOL_MESHCORE);
    CHECK(!packet.has_source);
    CHECK(has_detail(&packet, "Route       direct, 2 hops"));
    CHECK(strcmp(packet.summary, "text 7E>5D 2h") == 0);

    uint8_t junk[] = {0xC5, 0x01, 0x02};
    decode_packet(junk, sizeof(junk), 0x12, false, &packet);
    CHECK(packet.protocol == PROTOCOL_UNKNOWN);
    CHECK(strcmp(packet.summary, "C50102") == 0);
}

static void test_meshtastic_slots(void) {
    CHECK(meshtastic_djb2("LongFast") == 130429955u);
    CHECK(meshtastic_default_frequency(902000000, 928000000, 250, "LongFast") == 906875000);
    CHECK(meshtastic_default_frequency(869400000, 869650000, 250, "LongFast") == 869525000);
}

static void test_aes_ctr_rfc3686(void) {
    uint8_t key[16], counter[16], expected[16], output[16];
    from_hex("AE6852F8121067CC4BF7A5765577F39E", key);
    from_hex("00000030000000000000000000000001", counter);
    from_hex("E4095D4FB7A7B3792D6175A3261311B8", expected);
    CHECK(aes128_ctr(key, counter, (const uint8_t*)"Single block msg", output, 16));
    CHECK(memcmp(output, expected, 16) == 0);
}

static void test_meshtastic_data_from_firmware(void) {
    uint8_t           data[16];
    size_t            length = from_hex("08011204746573744800", data);
    meshtastic_data_t parsed;
    CHECK(meshtastic_parse_data(data, length, &parsed));
    CHECK(parsed.portnum == 1 && parsed.has_text && strcmp(parsed.text, "test") == 0);
}

static void test_meshtastic_default_key_nodeinfo(void) {
    uint8_t          data[128];
    size_t           length = from_hex(
        "ffffffff78563412d4c3b2a16308009a6bc453508351307c446b310917b64e9b88ed62f610cff10a3368f9f101bf7b70af7d4a", data);
    decoded_packet_t packet;
    decode_packet(data, length, 0x2B, false, &packet);
    CHECK(packet.protocol == PROTOCOL_MESHTASTIC);
    CHECK(strcmp(packet.source_label, "Porch Node") == 0);
    CHECK(has_detail(&packet, "Name        Porch Node (PRCH)"));
    CHECK(has_detail(&packet, "User ID     !12345678, hardware model 43"));
    CHECK(strcmp(packet.summary, "!12345678 is Porch Node") == 0);
}

static void test_meshtastic_default_key_text(void) {
    uint8_t          data[128];
    size_t           length = from_hex("ffffffff785634120df0ad0b6308009a2e168bcaab66f6714d092dc758a0f7e53d07", data);
    decoded_packet_t packet;
    decode_packet(data, length, 0x2B, false, &packet);
    CHECK(has_detail(&packet, "App         text message (port 1)"));
    CHECK(has_detail(&packet, "Text        hello mesh ?"));
    CHECK(strcmp(packet.summary, "!12345678: hello mesh ?") == 0);
}

static void test_meshtastic_default_key_position(void) {
    uint8_t          data[128];
    size_t           length = from_hex("ffffffff0d0c0b0a010000006308009a5ceade1df4e7cc6c4f998efa558c8be3", data);
    decoded_packet_t packet;
    decode_packet(data, length, 0x2B, false, &packet);
    CHECK(has_detail(&packet, "Location    42.38700, -71.09900"));
    CHECK(has_detail(&packet, "Altitude    15 m"));
    CHECK(strcmp(packet.summary, "!0a0b0c0d at 42.387,-71.099") == 0);
}

static void test_meshtastic_preset_hashes(void) {
    CHECK(meshtastic_channel_hash("LongFast") == 0x08);
    CHECK(meshtastic_channel_hash("MediumFast") == 0x1F);
    CHECK(meshtastic_channel_hash("ShortFast") == 0x70);
    CHECK(meshtastic_channel_hash("LongSlow") == 0x0F);
    CHECK(meshtastic_channel_hash("MediumSlow") == 0x18);
    CHECK(meshtastic_channel_hash("LongModerate") == 0x09);
    CHECK(meshtastic_channel_hash("ShortTurbo") == 0x0E);

    uint8_t          data[128];
    size_t           length = from_hex("ffffffff0d0c0b0a010000006308009a5ceade1df4e7cc6c4f998efa558c8be3", data);
    data[13]                = 0x1F;
    decoded_packet_t packet;
    decode_packet(data, length, 0x2B, false, &packet);
    CHECK(has_detail(&packet, "hash 0x1F (MediumFast, default key)"));
    CHECK(has_detail(&packet, "Location    42.38700, -71.09900"));
}

static void test_meshtastic_other_channel_stays_encrypted(void) {
    uint8_t          data[128];
    size_t           length = from_hex("ffffffff0d0c0b0a010000006308009a5ceade1df4e7cc6c4f998efa558c8be3", data);
    data[13]                = 0x51;
    decoded_packet_t packet;
    decode_packet(data, length, 0x2B, false, &packet);
    CHECK(has_detail(&packet, "Payload     16 bytes, encrypted"));
    CHECK(strcmp(packet.summary, "!0a0b0c0d>all hop 0/3") == 0);
}

static void test_random_packets_do_not_crash(void) {
    static const uint8_t sync_words[] = {0x12, 0x34, 0x2B, 0x99};
    uint8_t              data[256];
    srand(1234);
    for (int round = 0; round < 300000; round++) {
        uint8_t length = rand() % 256;
        for (int i = 0; i < length; i++) {
            data[i] = rand();
        }
        if (round % 3 == 0 && length > 0) {
            data[0] = (data[0] & 0xE3) | 0x10;
        }
        if (round % 4 == 2 && length > 13) {
            data[13] = 0x08;
        }
        decoded_packet_t packet;
        decode_packet(data, length, sync_words[round % 4], round & 1, &packet);
        CHECK(memchr(packet.summary, '\0', sizeof(packet.summary)) != NULL);
        CHECK(memchr(packet.source_label, '\0', sizeof(packet.source_label)) != NULL);
        CHECK(packet.detail_count <= DECODE_MAX_DETAILS);
    }
}

int main(void) {
    test_netid();
    test_lorawan_data_uplink();
    test_lorawan_downlink_with_fopts();
    test_lorawan_join_request();
    test_lorawan_bad_lengths();
    test_meshtastic();
    test_meshcore_advert();
    test_meshcore_text_and_garbage();
    test_meshtastic_slots();
    test_aes_ctr_rfc3686();
    test_meshtastic_data_from_firmware();
    test_meshtastic_default_key_nodeinfo();
    test_meshtastic_default_key_text();
    test_meshtastic_default_key_position();
    test_meshtastic_preset_hashes();
    test_meshtastic_other_channel_stays_encrypted();
    test_random_packets_do_not_crash();
    if (failures) {
        fprintf(stderr, "%d check(s) failed\n", failures);
        return EXIT_FAILURE;
    }
    printf("all decoder tests passed\n");
    return EXIT_SUCCESS;
}
