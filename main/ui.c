#include "ui.h"
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include "bsp/power.h"
#include "esp_timer.h"
#include "pax_fonts.h"
#include "pax_text.h"

static pax_buf_t* fb;
static uint8_t*   raw_pixels;
static bool       direct_columns;
static int        raw_width;

void ui_init(pax_buf_t* framebuffer, bool reversed_endianness) {
    fb             = framebuffer;
    raw_width      = pax_buf_get_width_raw(fb);
    raw_pixels     = pax_buf_get_pixels_rw(fb);
    direct_columns = pax_buf_get_type(fb) == PAX_BUF_24_888RGB && pax_buf_get_orientation(fb) == PAX_O_ROT_CW &&
                     !reversed_endianness && raw_width == UI_HEIGHT;
}

pax_buf_t* ui_fb(void) {
    return fb;
}

void ui_text(int x, int y, uint32_t color, const char* fmt, ...) {
    char    text[160];
    va_list args;
    va_start(args, fmt);
    vsnprintf(text, sizeof(text), fmt, args);
    va_end(args);
    pax_draw_text(fb, color, pax_font_sky_mono, UI_FONT_SIZE, x, y, text);
}

void ui_text_right(int x_right, int y, uint32_t color, const char* fmt, ...) {
    char    text[160];
    va_list args;
    va_start(args, fmt);
    vsnprintf(text, sizeof(text), fmt, args);
    va_end(args);
    pax_vec2f size = pax_text_size(pax_font_sky_mono, UI_FONT_SIZE, text);
    pax_draw_text(fb, color, pax_font_sky_mono, UI_FONT_SIZE, x_right - size.x, y, text);
}

void ui_rect(int x, int y, int w, int h, uint32_t color) {
    pax_simple_rect(fb, color, x, y, w, h);
}

void ui_hline(int x0, int x1, int y, uint32_t color) {
    pax_simple_rect(fb, color, x0, y, x1 - x0 + 1, 1);
}

void ui_vline(int x, int y0, int y1, uint32_t color) {
    pax_simple_rect(fb, color, x, y0, 1, y1 - y0 + 1);
}

void ui_line(int x0, int y0, int x1, int y1, uint32_t color) {
    pax_simple_line(fb, color, x0, y0, x1, y1);
}

void ui_column(int x, int y_top, const uint32_t* colors, int count) {
    if (x < 0 || x >= UI_WIDTH) {
        return;
    }
    if (!direct_columns) {
        for (int i = 0; i < count; i++) {
            pax_set_pixel(fb, colors[i], x, y_top + i);
        }
        return;
    }
    for (int i = 0; i < count; i++) {
        int y = y_top + i;
        if (y < 0 || y >= UI_HEIGHT) {
            continue;
        }
        uint8_t* pixel = raw_pixels + 3 * ((size_t)x * raw_width + (raw_width - 1 - y));
        pixel[0]       = colors[i];
        pixel[1]       = colors[i] >> 8;
        pixel[2]       = colors[i] >> 16;
    }
}

static int battery_percentage(void) {
    static int64_t last_read_us = 0;
    static int     percentage   = -1;
    int64_t        now          = esp_timer_get_time();
    if (last_read_us == 0 || now - last_read_us > 10 * 1000 * 1000) {
        last_read_us                         = now;
        bsp_power_battery_information_t info = {0};
        if (bsp_power_get_battery_information(&info) == ESP_OK && info.battery_available) {
            percentage = (int)(info.remaining_percentage + 0.5);
        }
    }
    return percentage;
}

void ui_draw_frame(view_t active, const char* hints) {
    static const char* const names[VIEW_COUNT] = {"Spectrum", "Listen", "Devices", "Radio"};

    ui_rect(0, 0, UI_WIDTH, UI_HEADER_H, COLOR_PANEL);
    ui_text(8, 6, COLOR_ACCENT, "kikite");
    int x = 8 + 8 * UI_CHAR_W;
    for (int view = 0; view < VIEW_COUNT; view++) {
        char label[24];
        snprintf(label, sizeof(label), "%d %s", view + 1, names[view]);
        int width = (int)strlen(label) * UI_CHAR_W + 12;
        if (view == (int)active) {
            ui_rect(x - 6, 2, width, UI_HEADER_H - 4, COLOR_SELECTED);
        }
        ui_text(x, 6, view == (int)active ? COLOR_TEXT : COLOR_DIM, "%s", label);
        x += width + 6;
    }
    int battery = battery_percentage();
    if (battery >= 0) {
        ui_text_right(UI_WIDTH - 8, 6, battery < 15 ? COLOR_BAD : COLOR_DIM, "%d%%", battery);
    }

    ui_rect(0, UI_HEIGHT - UI_FOOTER_H, UI_WIDTH, UI_FOOTER_H, COLOR_PANEL);
    pax_draw_text(fb, COLOR_DIM, pax_font_sky, UI_FONT_SIZE, 8, UI_HEIGHT - UI_FOOTER_H + 4, hints);
}

uint32_t ui_heat_color(float level) {
    static const uint8_t stops[][3] = {
        {0, 0, 0}, {20, 10, 90}, {0, 110, 200}, {0, 200, 160}, {240, 230, 40}, {255, 90, 20}, {255, 255, 255},
    };
    const int stop_count = sizeof(stops) / sizeof(stops[0]);
    if (level <= 0.0f) {
        level = 0.0f;
    }
    if (level >= 1.0f) {
        level = 1.0f;
    }
    float    position = level * (stop_count - 1);
    int      index    = (int)position;
    if (index >= stop_count - 1) {
        index = stop_count - 2;
    }
    float    fraction = position - index;
    uint32_t color    = 0xFF000000;
    for (int channel = 0; channel < 3; channel++) {
        float value = stops[index][channel] + (stops[index + 1][channel] - stops[index][channel]) * fraction;
        color |= (uint32_t)(value + 0.5f) << (16 - 8 * channel);
    }
    return color;
}

bool ui_shift(const ui_key_t* key) {
    return (key->modifiers & BSP_INPUT_MODIFIER_SHIFT) != 0;
}

void ui_format_ago(char* out, size_t size, int64_t then_us, int64_t now_us) {
    int64_t seconds = (now_us - then_us) / 1000000;
    if (seconds < 60) {
        snprintf(out, size, "%llds", (long long)seconds);
    } else if (seconds < 3600) {
        snprintf(out, size, "%lldm", (long long)(seconds / 60));
    } else {
        snprintf(out, size, "%lldh", (long long)(seconds / 3600));
    }
}
