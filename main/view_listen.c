#include <stdio.h>
#include <string.h>
#include "app.h"
#include "capture.h"
#include "esp_timer.h"

#define LIST_TOP     (UI_CONTENT_TOP + 90)
#define VISIBLE_ROWS ((UI_CONTENT_BOT - LIST_TOP) / UI_LINE_H)

static const uint8_t  custom_sync_words[]  = {0x12, 0x34, 0x2B};
static const uint16_t custom_bandwidths[]  = {62, 125, 250, 500};
static uint8_t        profile_index        = 0;
static uint32_t       custom_frequency_hz  = 0;
static uint8_t        custom_sf            = 7;
static uint8_t        custom_bw_index      = 1;
static uint8_t        custom_sync_index    = 0;
static uint32_t       selected_number      = 0;
static uint32_t       list_scroll          = 0;
static bool           showing_detail       = false;
static listen_params_t active_params;

static uint8_t profile_count(void) {
    uint8_t count = 0;
    profiles_listen(app_region(), &count);
    return count + 1;
}

static bool custom_selected(void) {
    return profile_index == profile_count() - 1;
}

static const char* profile_name(void) {
    if (custom_selected()) {
        return "Custom (spectrum cursor)";
    }
    uint8_t count;
    return profiles_listen(app_region(), &count)[profile_index].name;
}

static void build_params(listen_params_t* params) {
    memset(params, 0, sizeof(*params));
    if (custom_selected()) {
        if (custom_frequency_hz == 0) {
            const sweep_range_t* ranges;
            uint8_t              count;
            ranges              = profiles_sweep_ranges(app_region(), &count);
            custom_frequency_hz = (ranges[0].start_hz + ranges[0].stop_hz) / 2;
        }
        params->channel_count = 1;
        params->dwell_ms      = 0;
        params->channels[0]   = (listen_channel_t){
              .frequency_hz     = custom_frequency_hz,
              .spreading_factor = custom_sf,
              .bandwidth_khz    = custom_bandwidths[custom_bw_index],
              .coding_rate      = 5,
              .sync_word        = custom_sync_words[custom_sync_index],
              .preamble_length  = 8,
              .invert_iq        = false,
        };
        return;
    }
    uint8_t                 count;
    const listen_profile_t* profiles = profiles_listen(app_region(), &count);
    profiles[profile_index].build(params, custom_frequency_hz);
}

void view_listen_activate(void) {
    build_params(&active_params);
    radio_request_listen(&active_params);
}

void view_listen_region_changed(void) {
    profile_index       = 0;
    custom_frequency_hz = 0;
}

void view_listen_set_cursor_frequency(uint32_t frequency_hz) {
    custom_frequency_hz = frequency_hz;
    profile_index       = profile_count() - 1;
}

static void format_mhz(char* out, size_t size, uint32_t hz) {
    snprintf(out, size, "%lu.%03lu", (unsigned long)(hz / 1000000), (unsigned long)(hz % 1000000 / 1000));
}

static const char* bandwidth_text(uint16_t bandwidth_khz) {
    switch (bandwidth_khz) {
        case 62:
            return "62.5";
        case 125:
            return "125";
        case 250:
            return "250";
        case 500:
            return "500";
        default:
            return "?";
    }
}

static void format_channel(char* out, size_t size, const listen_channel_t* channel) {
    char mhz[16];
    format_mhz(mhz, sizeof(mhz), channel->frequency_hz);
    snprintf(out, size, "%s MHz SF%u BW%s sync 0x%02X%s", mhz, channel->spreading_factor,
             bandwidth_text(channel->bandwidth_khz), channel->sync_word, channel->invert_iq ? " IQ-inv" : "");
}

static int find_selected_age(void) {
    uint32_t count = capture_packet_count();
    for (uint32_t age = 0; age < count; age++) {
        if (capture_packet(age)->number == selected_number) {
            return (int)age;
        }
    }
    return -1;
}

static void draw_detail(const captured_packet_t* packet, int64_t now) {
    char line[96];
    char ago[12];
    int  y = UI_CONTENT_TOP + 4;
    ui_format_ago(ago, sizeof(ago), packet->received_us, now);
    format_channel(line, sizeof(line), &packet->channel);

    ui_text(8, y, COLOR_ACCENT, "Packet #%lu  %s ago  %s", (unsigned long)packet->number, ago,
            decode_protocol_name(packet->decoded.protocol));
    y += UI_LINE_H;
    ui_text(8, y, COLOR_TEXT, "%s", line);
    y += UI_LINE_H;
    if (packet->snr_db_x4 == CAPTURE_SNR_UNKNOWN) {
        ui_text(8, y, COLOR_TEXT, "Signal %d dBm  SNR n/a  %u bytes", packet->rssi_dbm, packet->length);
    } else {
        ui_text(8, y, COLOR_TEXT, "Signal %d dBm  SNR %.2f dB  %u bytes", packet->rssi_dbm, packet->snr_db_x4 / 4.0f,
                packet->length);
    }
    y += UI_LINE_H;
    ui_text(8, y, COLOR_DIM, "raw radio stats %02X %02X %02X", packet->raw_stats[0], packet->raw_stats[1],
            packet->raw_stats[2]);
    y += UI_LINE_H + 6;

    for (uint8_t i = 0; i < packet->decoded.detail_count && y < UI_CONTENT_BOT - UI_LINE_H; i++) {
        ui_text(8, y, COLOR_TEXT, "%s", packet->decoded.details[i]);
        y += UI_LINE_H;
    }
    y += 6;

    for (int offset = 0; offset < packet->length && y < UI_CONTENT_BOT - UI_LINE_H; offset += 16) {
        int length = snprintf(line, sizeof(line), "%02X:", offset);
        for (int i = offset; i < offset + 16 && i < packet->length; i++) {
            length += snprintf(line + length, sizeof(line) - length, " %02X", packet->data[i]);
        }
        ui_text(8, y, COLOR_DIM, "%s", line);
        y += UI_LINE_H;
    }
}

void view_listen_draw(void) {
    int64_t      now = esp_timer_get_time();
    radio_info_t info;
    radio_get_info(&info);
    char line[96];

    capture_lock();
    if (showing_detail) {
        int age = find_selected_age();
        if (age >= 0) {
            draw_detail(capture_packet(age), now);
            capture_unlock();
            ui_draw_frame(VIEW_LISTEN, "^v previous/next packet   Esc back   F1 exit");
            return;
        }
        showing_detail = false;
    }

    ui_text(8, UI_CONTENT_TOP + 2, COLOR_ACCENT, "%s", profile_name());
    ui_text_right(UI_WIDTH - 8, UI_CONTENT_TOP + 2, COLOR_DIM, "%lu packets",
                  (unsigned long)capture_total_received());
    uint8_t current = info.listen_channel_index < active_params.channel_count ? info.listen_channel_index : 0;
    format_channel(line, sizeof(line), &active_params.channels[current]);
    ui_text(8, UI_CONTENT_TOP + 24, COLOR_TEXT, "%s", line);
    if (active_params.channel_count > 1) {
        ui_text(8, UI_CONTENT_TOP + 44, COLOR_DIM, "hopping %u channels, %lu ms each, now %u", active_params.channel_count,
                (unsigned long)active_params.dwell_ms, current + 1);
    }

    bool hopping = active_params.channel_count > 1;
    ui_text(8, LIST_TOP - 22, COLOR_DIM, hopping ? "ago  MHz     rssi  snr len proto    summary"
                                                  : "ago rssi  snr len proto    summary");
    ui_hline(8, UI_WIDTH - 8, LIST_TOP - 2, COLOR_GRID);

    uint32_t count = capture_packet_count();
    int      selected_age = selected_number ? find_selected_age() : -1;
    if (selected_number && selected_age < 0) {
        selected_number = 0;
    }
    if (selected_age >= 0) {
        if ((uint32_t)selected_age < list_scroll) {
            list_scroll = selected_age;
        } else if ((uint32_t)selected_age >= list_scroll + VISIBLE_ROWS) {
            list_scroll = selected_age - VISIBLE_ROWS + 1;
        }
    } else {
        list_scroll = 0;
    }

    if (count == 0) {
        ui_text(8, LIST_TOP + 10, COLOR_DIM, "Listening... packets appear here as they arrive.");
    }
    for (uint32_t row = 0; row < VISIBLE_ROWS && list_scroll + row < count; row++) {
        const captured_packet_t* packet = capture_packet(list_scroll + row);
        int                      y      = LIST_TOP + row * UI_LINE_H;
        char                     ago[12];
        ui_format_ago(ago, sizeof(ago), packet->received_us, now);
        if ((int)(list_scroll + row) == selected_age) {
            ui_rect(4, y - 1, UI_WIDTH - 8, UI_LINE_H, COLOR_SELECTED);
        }
        uint32_t color = packet->decoded.protocol == PROTOCOL_UNKNOWN ? COLOR_DIM : COLOR_TEXT;
        char     snr[8];
        if (packet->snr_db_x4 == CAPTURE_SNR_UNKNOWN) {
            snprintf(snr, sizeof(snr), "--");
        } else {
            snprintf(snr, sizeof(snr), "%.0f", packet->snr_db_x4 / 4.0f);
        }
        if (hopping) {
            char mhz[16];
            format_mhz(mhz, sizeof(mhz), packet->channel.frequency_hz);
            ui_text(8, y, color, "%3s %7s %4d %4s %3u %-8.8s %.19s", ago, mhz, packet->rssi_dbm, snr,
                    packet->length, decode_protocol_name(packet->decoded.protocol), packet->decoded.summary);
        } else {
            ui_text(8, y, color, "%3s %4d %4s %3u %-8.8s %.27s", ago, packet->rssi_dbm, snr, packet->length,
                    decode_protocol_name(packet->decoded.protocol), packet->decoded.summary);
        }
    }
    capture_unlock();

    if (custom_selected()) {
        ui_draw_frame(VIEW_LISTEN, "^v select  Enter details  P profile  F sf  W bw  Y sync  C clear");
    } else {
        ui_draw_frame(VIEW_LISTEN, "^v select  Enter details  P profile  C clear  F1 exit");
    }
}

static void move_selection(int direction) {
    capture_lock();
    uint32_t count = capture_packet_count();
    if (count == 0) {
        capture_unlock();
        return;
    }
    int age = selected_number ? find_selected_age() : -1;
    if (age < 0) {
        age = direction > 0 ? 0 : -1;
    } else {
        age += direction;
    }
    if (age < 0) {
        selected_number = 0;
    } else {
        if ((uint32_t)age >= count) {
            age = count - 1;
        }
        selected_number = capture_packet(age)->number;
    }
    capture_unlock();
}

void view_listen_key(const ui_key_t* key) {
    if (key->is_navigation) {
        switch (key->key) {
            case BSP_INPUT_NAVIGATION_KEY_UP:
                move_selection(-1);
                break;
            case BSP_INPUT_NAVIGATION_KEY_DOWN:
                move_selection(1);
                break;
            case BSP_INPUT_NAVIGATION_KEY_RETURN:
                if (selected_number == 0) {
                    move_selection(1);
                }
                showing_detail = selected_number != 0;
                break;
            case BSP_INPUT_NAVIGATION_KEY_ESC:
                if (showing_detail) {
                    showing_detail = false;
                } else {
                    selected_number = 0;
                }
                break;
            default:
                break;
        }
        return;
    }

    bool retune = false;
    switch (key->ascii) {
        case '\b':
            showing_detail = false;
            break;
        case 'p':
            profile_index = (profile_index + 1) % profile_count();
            retune        = true;
            break;
        case 'P':
            profile_index = (profile_index + profile_count() - 1) % profile_count();
            retune        = true;
            break;
        case 'c':
        case 'C':
            capture_clear();
            selected_number = 0;
            showing_detail  = false;
            break;
        case 'f':
        case 'F':
            if (custom_selected()) {
                custom_sf = custom_sf >= 12 ? 7 : custom_sf + 1;
                retune    = true;
            }
            break;
        case 'w':
        case 'W':
            if (custom_selected()) {
                custom_bw_index = (custom_bw_index + 1) % (sizeof(custom_bandwidths) / sizeof(custom_bandwidths[0]));
                retune          = true;
            }
            break;
        case 'y':
        case 'Y':
            if (custom_selected()) {
                custom_sync_index = (custom_sync_index + 1) % sizeof(custom_sync_words);
                retune            = true;
            }
            break;
        default:
            break;
    }
    if (retune) {
        view_listen_activate();
    }
}
