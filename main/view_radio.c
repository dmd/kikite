#include <stdio.h>
#include "app.h"
#include "capture.h"

void view_radio_draw(void) {
    radio_info_t info;
    radio_get_info(&info);
    int y = UI_CONTENT_TOP + 8;

    ui_text(8, y, COLOR_ACCENT, "Radio");
    y += UI_LINE_H + 4;
    ui_text(8, y, COLOR_TEXT, "Chip            %s (%s)", info.version,
            info.chip_type == LORA_PROTOCOL_CHIP_SX1268 ? "433 MHz module" : "868/915 MHz module");
    y += UI_LINE_H;
    ui_text(8, y, COLOR_TEXT, "Region          %s%s", profiles_region_name(app_region()),
            app_region_switchable() ? "   (G to change)" : "");
    y += UI_LINE_H;
    ui_text(8, y, info.device_errors ? COLOR_WARN : COLOR_TEXT, "Device errors   0x%04x", info.device_errors);
    y += UI_LINE_H;
    ui_text(8, y, info.rpc_errors ? COLOR_WARN : COLOR_TEXT, "Command errors  %lu", (unsigned long)info.rpc_errors);
    y += UI_LINE_H;
    ui_text(8, y, info.rssi_supported ? COLOR_GOOD : COLOR_DIM, "Signal readings %s",
            info.rssi_supported ? "working" : "not confirmed yet");
    y += UI_LINE_H + 12;

    ui_text(8, y, COLOR_ACCENT, "Sweep timing (moving average)");
    y += UI_LINE_H + 4;
    ui_text(8, y, COLOR_TEXT, "Mode changes    %6.2f ms per step", info.timing.mode_us / 1000.0f);
    y += UI_LINE_H;
    ui_text(8, y, COLOR_TEXT, "Retune          %6.2f ms per step", info.timing.config_us / 1000.0f);
    y += UI_LINE_H;
    ui_text(8, y, COLOR_TEXT, "Signal reading  %6.2f ms each", info.timing.rssi_us / 1000.0f);
    y += UI_LINE_H;
    ui_text(8, y, COLOR_TEXT, "Whole step      %6.2f ms", info.timing.step_us / 1000.0f);
    y += UI_LINE_H;
    ui_text(8, y, COLOR_TEXT, "Last sweep      %6.2f s", info.last_sweep_ms / 1000.0f);
    y += UI_LINE_H;
    ui_text(8, y, COLOR_DIM, "M on the Spectrum tab changes the sweep method.");
    y += UI_LINE_H + 12;

    ui_text(8, y, COLOR_ACCENT, "Capture");
    y += UI_LINE_H + 4;
    ui_text(8, y, COLOR_TEXT, "Packets received %lu", (unsigned long)capture_total_received());
    y += UI_LINE_H + 12;

    ui_text(8, y, COLOR_DIM, "kikite only listens; it never transmits.");
    y += UI_LINE_H;
    ui_text(8, y, COLOR_DIM, "On exit (F1) the radio's previous LoRa settings");
    y += UI_LINE_H;
    ui_text(8, y, COLOR_DIM, "are restored, so other apps find them unchanged.");
    ui_draw_frame(VIEW_RADIO, app_region_switchable() ? "G change region   F1 exit" : "F1 exit");
}

void view_radio_key(const ui_key_t* key) {
    if (!key->is_navigation && (key->ascii == 'g' || key->ascii == 'G') && app_region_switchable()) {
        app_set_region(app_region() == REGION_US915 ? REGION_EU868 : REGION_US915);
    }
}
