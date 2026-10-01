#include "telemetry.h"
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include "bsp/input.h"
#include "esp_heap_caps.h"
#include "freertos/task.h"
#include "spectrum.h"
#include "app.h"
#include "ui.h"

#define LINE_LENGTH 256

static volatile bool enabled;
static QueueHandle_t keys;
static int16_t*      sweep_row;
static char*         sweep_line;

typedef struct {
    const char* name;
    uint32_t    key;
} nav_name_t;

static const nav_name_t nav_names[] = {
    {"left", BSP_INPUT_NAVIGATION_KEY_LEFT},     {"right", BSP_INPUT_NAVIGATION_KEY_RIGHT},
    {"up", BSP_INPUT_NAVIGATION_KEY_UP},         {"down", BSP_INPUT_NAVIGATION_KEY_DOWN},
    {"return", BSP_INPUT_NAVIGATION_KEY_RETURN}, {"esc", BSP_INPUT_NAVIGATION_KEY_ESC},
    {"f1", BSP_INPUT_NAVIGATION_KEY_F1},         {"f2", BSP_INPUT_NAVIGATION_KEY_F2},
    {"f3", BSP_INPUT_NAVIGATION_KEY_F3},         {"f4", BSP_INPUT_NAVIGATION_KEY_F4},
    {"f5", BSP_INPUT_NAVIGATION_KEY_F5},
};

bool telemetry_enabled(void) {
    return enabled;
}

static void print_status(void) {
    radio_info_t info;
    radio_get_info(&info);
    printf("@I rssi_ok=%d rssi_unsupported=%d rpc_errors=%lu device_errors=0x%04x mode_us=%lu config_us=%lu "
           "rssi_us=%lu step_us=%lu sweep_ms=%lu listen_index=%u listen_hz=%lu packets=%lu\n",
           info.rssi_supported, info.rssi_unsupported, (unsigned long)info.rpc_errors, info.device_errors,
           (unsigned long)info.timing.mode_us, (unsigned long)info.timing.config_us,
           (unsigned long)info.timing.rssi_us, (unsigned long)info.timing.step_us,
           (unsigned long)info.last_sweep_ms, info.listen_channel_index, (unsigned long)info.listen_frequency_hz,
           (unsigned long)capture_total_received());
}

static void push_key(ui_key_t key) {
    xQueueSend(keys, &key, pdMS_TO_TICKS(100));
}

static void handle_command(char* line) {
    size_t length = strlen(line);
    while (length && (line[length - 1] == '\r' || line[length - 1] == '\n' || line[length - 1] == ' ')) {
        line[--length] = '\0';
    }
    if (length == 0) {
        return;
    }
    if (strcmp(line, "telemetry on") == 0) {
        enabled = true;
    } else if (strcmp(line, "telemetry off") == 0) {
        enabled = false;
    } else if (strcmp(line, "display off") == 0) {
        app_request_display(false);
    } else if (strcmp(line, "display on") == 0) {
        app_request_display(true);
    } else if (strcmp(line, "screenshot") == 0) {
        app_request_screenshot();
    } else if (strcmp(line, "status") == 0) {
        print_status();
    } else if (strncmp(line, "key ", 4) == 0) {
        for (const char* c = line + 4; *c; c++) {
            push_key((ui_key_t){.ascii = *c});
        }
    } else if (strncmp(line, "nav ", 4) == 0) {
        const char* name  = line + 4;
        bool        found = false;
        for (size_t i = 0; i < sizeof(nav_names) / sizeof(nav_names[0]); i++) {
            if (strcmp(name, nav_names[i].name) == 0) {
                push_key((ui_key_t){.is_navigation = true, .key = nav_names[i].key});
                found = true;
            }
        }
        if (!found) {
            printf("@E unknown nav key '%s'\n", name);
            return;
        }
    } else {
        printf("@E unknown command '%s'\n", line);
        return;
    }
    printf("@A %s\n", line);
}

static void command_task(void* arg) {
    char   line[LINE_LENGTH];
    size_t used = 0;
    while (1) {
        char    chunk[64];
        ssize_t received = read(STDIN_FILENO, chunk, sizeof(chunk));
        if (received <= 0) {
            vTaskDelay(pdMS_TO_TICKS(50));
            continue;
        }
        for (ssize_t i = 0; i < received; i++) {
            if (chunk[i] == '\n' || chunk[i] == '\r') {
                line[used] = '\0';
                handle_command(line);
                used = 0;
            } else if (used + 1 < sizeof(line)) {
                line[used++] = chunk[i];
            }
        }
    }
}

bool telemetry_start(QueueHandle_t key_queue) {
    keys       = key_queue;
    sweep_row  = heap_caps_malloc(SPECTRUM_MAX_BINS * sizeof(int16_t), MALLOC_CAP_SPIRAM);
    sweep_line = heap_caps_malloc(SPECTRUM_MAX_BINS * 6 + 256, MALLOC_CAP_SPIRAM);
    if (!sweep_row || !sweep_line) {
        return false;
    }
    return xTaskCreate(command_task, "telemetry", 4096, NULL, 2, NULL) == pdPASS;
}

void telemetry_sweep(const sweep_params_t* params, uint32_t sweep_ms) {
    if (!enabled) {
        return;
    }
    const spectrum_t* s     = spectrum_lock();
    const int16_t*    row   = spectrum_history_row(s, 0);
    uint16_t          bins  = s->bins;
    uint32_t          count = s->completed_sweeps;
    if (row) {
        memcpy(sweep_row, row, bins * sizeof(int16_t));
    }
    spectrum_unlock();
    if (!row) {
        return;
    }

    radio_info_t info;
    radio_get_info(&info);
    int used = snprintf(sweep_line, 256, "@S n=%lu start=%lu step=%lu bins=%u bw=%u method=%d samples=%u ms=%lu "
                                         "rpc_errors=%lu device_errors=0x%04x data=",
                        (unsigned long)count, (unsigned long)params->start_hz, (unsigned long)params->step_hz, bins,
                        params->bandwidth_khz, params->method, params->samples_per_step, (unsigned long)sweep_ms,
                        (unsigned long)info.rpc_errors, info.device_errors);
    for (uint16_t bin = 0; bin < bins; bin++) {
        if (sweep_row[bin] == SPECTRUM_NO_DATA) {
            used += sprintf(sweep_line + used, bin ? ",x" : "x");
        } else {
            used += sprintf(sweep_line + used, bin ? ",%d" : "%d", sweep_row[bin]);
        }
    }
    sweep_line[used++] = '\n';
    fwrite(sweep_line, 1, used, stdout);
    fflush(stdout);
}

void telemetry_packet(const captured_packet_t* packet) {
    if (!enabled) {
        return;
    }
    char hex[2 * 256 + 1];
    for (int i = 0; i < packet->length; i++) {
        sprintf(hex + 2 * i, "%02X", packet->data[i]);
    }
    hex[2 * packet->length] = '\0';
    printf("@P n=%lu f=%lu sf=%u bw=%u sync=0x%02X iq=%d sig=%d snr4=%d raw=%02X%02X%02X len=%u proto=%s "
           "summary=\"%s\" data=%s\n",
           (unsigned long)packet->number, (unsigned long)packet->channel.frequency_hz,
           packet->channel.spreading_factor, packet->channel.bandwidth_khz, packet->channel.sync_word,
           packet->channel.invert_iq, packet->rssi_dbm, packet->snr_db_x4, packet->raw_stats[0],
           packet->raw_stats[1], packet->raw_stats[2], packet->length, decode_protocol_name(packet->decoded.protocol),
           packet->decoded.summary, hex);
    fflush(stdout);
}

static const char base64_alphabet[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

static void emit_base64(const uint8_t* bytes, size_t length) {
    char line[4 + 4 * 256 / 3 + 8];
    int  used = sprintf(line, "@F ");
    for (size_t i = 0; i < length; i += 3) {
        uint32_t chunk = (uint32_t)bytes[i] << 16;
        if (i + 1 < length) {
            chunk |= (uint32_t)bytes[i + 1] << 8;
        }
        if (i + 2 < length) {
            chunk |= bytes[i + 2];
        }
        line[used++] = base64_alphabet[(chunk >> 18) & 0x3F];
        line[used++] = base64_alphabet[(chunk >> 12) & 0x3F];
        line[used++] = i + 1 < length ? base64_alphabet[(chunk >> 6) & 0x3F] : '=';
        line[used++] = i + 2 < length ? base64_alphabet[chunk & 0x3F] : '=';
    }
    line[used++] = '\n';
    fwrite(line, 1, used, stdout);
}

void telemetry_framebuffer(const uint8_t* pixels, int raw_width, int raw_height) {
    uint8_t  block[255];
    size_t   block_used = 0;
    size_t   total      = (size_t)raw_width * raw_height;
    uint32_t encoded    = 0;
    printf("@F begin width=%d height=%d format=bgr24-rle orientation=cw\n", raw_width, raw_height);
    size_t index = 0;
    while (index < total) {
        const uint8_t* pixel = &pixels[index * 3];
        size_t         run   = 1;
        while (index + run < total && run < 255 && memcmp(&pixels[(index + run) * 3], pixel, 3) == 0) {
            run++;
        }
        if (block_used + 4 > sizeof(block)) {
            emit_base64(block, block_used);
            encoded += block_used;
            block_used = 0;
        }
        block[block_used++] = (uint8_t)run;
        memcpy(&block[block_used], pixel, 3);
        block_used += 3;
        index += run;
    }
    if (block_used) {
        emit_base64(block, block_used);
        encoded += block_used;
    }
    printf("@F end bytes=%lu\n", (unsigned long)encoded);
    fflush(stdout);
}
