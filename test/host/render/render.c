#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "app.h"
#include "capture.h"
#include "pax_gfx.h"
#include "spectrum.h"

int64_t fake_now_us = 1000000;

static region_t     region = REGION_US915;
static radio_info_t fake_info;

region_t app_region(void) {
    return region;
}

bool app_region_switchable(void) {
    return true;
}

void app_set_region(region_t new_region) {
    region = new_region;
}

void app_show(view_t view) {
}

void app_listen_at(uint32_t frequency_hz) {
    view_listen_set_cursor_frequency(frequency_hz);
}

void app_radio_activity_changed(void) {
}

bool telemetry_enabled(void) {
    return false;
}

void telemetry_packet(const captured_packet_t* packet) {
}

void radio_request_idle(void) {
}

void radio_request_sweep(const sweep_params_t* params) {
}

void radio_request_listen(const listen_params_t* params) {
}

void radio_get_info(radio_info_t* out_info) {
    *out_info = fake_info;
}

const char* radio_sweep_method_name(sweep_method_t method) {
    static const char* const names[] = {"retune", "retune+RX", "standby+retune+RX"};
    return method < SWEEP_METHOD_COUNT ? names[method] : "?";
}

static void save(pax_buf_t* fb, const char* path) {
    FILE* file = fopen(path, "wb");
    fprintf(file, "P6\n%d %d\n255\n", UI_WIDTH, UI_HEIGHT);
    for (int y = 0; y < UI_HEIGHT; y++) {
        for (int x = 0; x < UI_WIDTH; x++) {
            pax_col_t color = pax_get_pixel(fb, x, y);
            fputc((color >> 16) & 0xFF, file);
            fputc((color >> 8) & 0xFF, file);
            fputc(color & 0xFF, file);
        }
    }
    fclose(file);
}

static double noise(void) {
    return (rand() / (double)RAND_MAX - 0.5) * 4.0;
}

static void fill_spectrum(void) {
    const uint32_t start = 902000000, step = 125000;
    const uint16_t bins  = 209;
    for (int sweep = 0; sweep < 230; sweep++) {
        spectrum_begin_sweep(start, step, bins);
        uint16_t limit = sweep == 229 ? 120 : bins;
        for (uint16_t bin = 0; bin < limit; bin++) {
            double   dbm = -117 + noise();
            uint32_t hz  = start + bin * step;
            if (hz >= 914875000 && hz <= 915125000) {
                dbm = -80 + noise();
            }
            if (sweep % 17 == 3 && hz >= 906750000 && hz <= 907000000) {
                dbm = -72 + noise();
            }
            if (rand() % 400 == 0) {
                dbm = -95 + noise();
            }
            if (hz >= 925000000 && hz <= 925500000 && sweep % 9 < 2) {
                dbm = -100 + noise();
            }
            spectrum_store_bin(bin, (int16_t)(dbm * 2));
        }
        if (limit == bins) {
            spectrum_end_sweep();
        }
    }
}

static void add_packet(const uint8_t* data, uint8_t length, const listen_channel_t* channel, int rssi, float snr) {
    lora_protocol_lora_packet_t packet = {0};
    packet.stats.rssi_pkt_raw          = 0;
    packet.stats.snr_pkt_raw           = (int8_t)rssi;
    packet.stats.signal_rssi_pkt_raw   = 0;
    packet.length                      = length;
    memcpy(packet.data, data, length);
    capture_add(&packet, channel);
    fake_now_us += 7300000;
}

static void fill_packets(void) {
    listen_channel_t meshtastic = {906875000, 11, 250, 5, 0x2B, 16, false};
    listen_channel_t meshcore   = {910525000, 7, 62, 5, 0x12, 32, false};
    listen_channel_t lorawan    = {904300000, 7, 125, 5, 0x34, 8, false};

    uint8_t mt[40] = {0xFF, 0xFF, 0xFF, 0xFF, 0x78, 0x56, 0x34, 0x12, 0x01, 0x02, 0x03, 0x04, 0x63, 0x08, 0, 0x9A};
    add_packet(mt, 40, &meshtastic, -104, 6.5);

    uint8_t advert[140] = {0x11, 0x00};
    for (int i = 0; i < 32; i++) {
        advert[2 + i] = 0xC0 + i;
    }
    int     offset         = 2 + 32 + 4 + 64;
    advert[offset++]       = 0x92;
    int32_t lat = 42387000, lon = -71099000;
    memcpy(&advert[offset], &lat, 4);
    memcpy(&advert[offset + 4], &lon, 4);
    offset += 8;
    memcpy(&advert[offset], "Spring St Rpt", 13);
    offset += 13;
    add_packet(advert, offset, &meshcore, -91, 9.25);

    uint8_t up[] = {0x40, 0x34, 0x12, 0x01, 0x26, 0x80, 0x2A, 0x00, 0x02, 0xAA, 0xBB, 0xCC, 0xDD, 0x11, 0x22, 0x33, 0x44};
    add_packet(up, sizeof(up), &lorawan, -118, -7.75);

    uint8_t helium[] = {0x80, 0x01, 0x00, 0x00, 0x78, 0x00, 0x10, 0x00, 0x05, 0xAA, 0xBB, 0x11, 0x22, 0x33, 0x44};
    add_packet(helium, sizeof(helium), &lorawan, -112, -2.0);

    uint8_t nodeinfo[64];
    size_t  nodeinfo_length = 0;
    const char* nodeinfo_hex = "ffffffff78563412d4c3b2a16308009a6bc453508351307c446b310917b64e9b88ed62f610cff10a3368f9f101bf7b70af7d4a";
    for (; nodeinfo_hex[2 * nodeinfo_length]; nodeinfo_length++) {
        unsigned value;
        sscanf(nodeinfo_hex + 2 * nodeinfo_length, "%2x", &value);
        nodeinfo[nodeinfo_length] = value;
    }
    add_packet(nodeinfo, nodeinfo_length, &meshtastic, -97, 4.25);

    uint8_t text_packet[64];
    size_t  text_length = 0;
    const char* text_hex = "ffffffff785634120df0ad0b6308009a2e168bcaab66f6714d092dc758a0f7e53d07";
    for (; text_hex[2 * text_length]; text_length++) {
        unsigned value;
        sscanf(text_hex + 2 * text_length, "%2x", &value);
        text_packet[text_length] = value;
    }
    add_packet(text_packet, text_length, &meshtastic, -99, 3.5);

    mt[4] = 0x99;
    mt[12] = 0x62;
    add_packet(mt, 40, &meshtastic, -121, -12.5);
    uint8_t text[] = {0x0A, 0x02, 0xAA, 0xBB, 0x5D, 0x7E, 1, 2, 3, 4, 5, 6, 7, 8};
    add_packet(text, sizeof(text), &meshcore, -99, 3.0);
}

int main(int argc, char** argv) {
    const char* out_dir = argc > 1 ? argv[1] : ".";
    char        path[512];

    pax_buf_t fb;
    pax_buf_init(&fb, NULL, 480, 800, PAX_BUF_24_888RGB);
    pax_buf_set_orientation(&fb, PAX_O_ROT_CW);
    ui_init(&fb, false);
    spectrum_init();
    capture_init();

    fake_info.available       = true;
    fake_info.rssi_supported  = true;
    fake_info.chip_type       = LORA_PROTOCOL_CHIP_SX1262;
    strcpy(fake_info.version, "SX1261 V2D 2D02");
    fake_info.timing          = (sweep_timing_t){.mode_us = 1800, .config_us = 3100, .rssi_us = 1400, .step_us = 7900};
    fake_info.last_sweep_ms   = 1650;
    fake_info.listen_channel_index = 0;

    srand(42);
    fill_spectrum();
    view_spectrum_activate();
    pax_background(&fb, COLOR_BG);
    view_spectrum_draw();
    snprintf(path, sizeof(path), "%s/spectrum.ppm", out_dir);
    save(&fb, path);

    fill_packets();
    view_listen_activate();
    pax_background(&fb, COLOR_BG);
    view_listen_draw();
    snprintf(path, sizeof(path), "%s/listen.ppm", out_dir);
    save(&fb, path);

    ui_key_t down = {.is_navigation = true, .key = BSP_INPUT_NAVIGATION_KEY_DOWN};
    ui_key_t enter = {.is_navigation = true, .key = BSP_INPUT_NAVIGATION_KEY_RETURN};
    view_listen_key(&down);
    view_listen_key(&down);
    view_listen_key(&down);
    view_listen_key(&down);
    view_listen_key(&enter);
    pax_background(&fb, COLOR_BG);
    view_listen_draw();
    snprintf(path, sizeof(path), "%s/detail.ppm", out_dir);
    save(&fb, path);

    ui_key_t custom = {.ascii = 'P'};
    ui_key_t escape = {.is_navigation = true, .key = BSP_INPUT_NAVIGATION_KEY_ESC};
    view_listen_key(&escape);
    view_listen_key(&custom);
    pax_background(&fb, COLOR_BG);
    view_listen_draw();
    snprintf(path, sizeof(path), "%s/listen_custom.ppm", out_dir);
    save(&fb, path);

    ui_key_t next = {.ascii = 'p'};
    view_listen_key(&next);
    view_listen_key(&next);
    view_listen_key(&next);
    view_listen_key(&next);
    fake_info.listen_channel_index = 5;
    pax_background(&fb, COLOR_BG);
    view_listen_draw();
    snprintf(path, sizeof(path), "%s/listen_hopping.ppm", out_dir);
    save(&fb, path);

    pax_background(&fb, COLOR_BG);
    view_devices_draw();
    snprintf(path, sizeof(path), "%s/devices.ppm", out_dir);
    save(&fb, path);

    pax_background(&fb, COLOR_BG);
    view_radio_draw();
    snprintf(path, sizeof(path), "%s/radio.ppm", out_dir);
    save(&fb, path);

    ui_key_t range = {.ascii = 'r'};
    ui_key_t bandwidth = {.ascii = 'b'};
    ui_key_t method = {.ascii = 'm'};
    view_spectrum_key(&range);
    view_spectrum_key(&range);
    view_spectrum_key(&range);
    view_spectrum_key(&bandwidth);
    view_spectrum_key(&bandwidth);
    view_spectrum_key(&bandwidth);
    view_spectrum_key(&method);
    pax_background(&fb, COLOR_BG);
    view_spectrum_draw();
    snprintf(path, sizeof(path), "%s/spectrum_wide.ppm", out_dir);
    save(&fb, path);

    fake_info.rssi_unsupported = true;
    pax_background(&fb, COLOR_BG);
    view_spectrum_draw();
    snprintf(path, sizeof(path), "%s/spectrum_unsupported.ppm", out_dir);
    save(&fb, path);
    return 0;
}
