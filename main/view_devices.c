#include <stdio.h>
#include "app.h"
#include "capture.h"
#include "esp_timer.h"

#define LIST_TOP   (UI_CONTENT_TOP + 50)
#define VISIBLE    ((UI_CONTENT_BOT - LIST_TOP) / UI_LINE_H)

static uint32_t scroll = 0;

void view_devices_draw(void) {
    int64_t now = esp_timer_get_time();
    capture_lock();
    uint32_t count = capture_device_count();
    if (scroll + VISIBLE > count) {
        scroll = count > VISIBLE ? count - VISIBLE : 0;
    }

    ui_text(8, UI_CONTENT_TOP + 2, COLOR_TEXT, "%lu transmitters heard", (unsigned long)count);
    ui_text(8, LIST_TOP - 22 + 2, COLOR_DIM, "%-8s %-16s %-11s %4s %4s %4s %3s", "proto", "id", "who", "pkts", "rssi",
            "best", "ago");
    ui_hline(8, UI_WIDTH - 8, LIST_TOP - 2, COLOR_GRID);

    if (count == 0) {
        ui_text(8, LIST_TOP + 10, COLOR_DIM, "Nothing yet. Listen on tab 2 to collect transmitters.");
    }
    for (uint32_t row = 0; row < VISIBLE && scroll + row < count; row++) {
        const device_t* device = capture_device_by_recency(scroll + row);
        char            ago[12];
        ui_format_ago(ago, sizeof(ago), device->last_seen_us, now);
        uint32_t color = now - device->last_seen_us < 10 * 1000000LL ? COLOR_TEXT : COLOR_DIM;
        ui_text(8, LIST_TOP + row * UI_LINE_H, color, "%-8.8s %-16.16s %-11.11s %4lu %4d %4d %3s",
                decode_protocol_name(device->protocol), device->source_text, device->label,
                (unsigned long)device->packets, device->last_rssi_dbm, device->best_rssi_dbm, ago);
    }
    capture_unlock();
    ui_draw_frame(VIEW_DEVICES, "^v scroll   C clear   F1 exit");
}

void view_devices_key(const ui_key_t* key) {
    if (key->is_navigation) {
        if (key->key == BSP_INPUT_NAVIGATION_KEY_UP && scroll > 0) {
            scroll--;
        } else if (key->key == BSP_INPUT_NAVIGATION_KEY_DOWN) {
            scroll++;
        }
        return;
    }
    if (key->ascii == 'c' || key->ascii == 'C') {
        capture_clear();
        scroll = 0;
    }
}
