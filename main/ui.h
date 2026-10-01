#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "bsp/input.h"
#include "pax_gfx.h"

#define UI_WIDTH        800
#define UI_HEIGHT       480
#define UI_HEADER_H     30
#define UI_FOOTER_H     26
#define UI_CONTENT_TOP  (UI_HEADER_H + 2)
#define UI_CONTENT_BOT  (UI_HEIGHT - UI_FOOTER_H - 2)
#define UI_FONT_SIZE    18
#define UI_CHAR_W       14
#define UI_LINE_H       20

#define COLOR_BG        0xFF000000
#define COLOR_PANEL     0xFF101820
#define COLOR_GRID      0xFF243040
#define COLOR_TEXT      0xFFE0E6EE
#define COLOR_DIM       0xFF7A8899
#define COLOR_ACCENT    0xFF39C5FF
#define COLOR_GOOD      0xFF5BE37D
#define COLOR_WARN      0xFFFFC14D
#define COLOR_BAD       0xFFFF5A5A
#define COLOR_SELECTED  0xFF1E3A5C
#define COLOR_TRACE     0xFFFFE066
#define COLOR_MAXHOLD   0xFFFF6B6B

typedef enum {
    VIEW_SPECTRUM,
    VIEW_LISTEN,
    VIEW_DEVICES,
    VIEW_RADIO,
    VIEW_COUNT,
} view_t;

typedef struct {
    bool     is_navigation;
    uint32_t key;
    char     ascii;
    uint32_t modifiers;
} ui_key_t;

void        ui_init(pax_buf_t* framebuffer, bool reversed_endianness);
pax_buf_t*  ui_fb(void);
void        ui_text(int x, int y, uint32_t color, const char* fmt, ...) __attribute__((format(printf, 4, 5)));
void        ui_text_right(int x_right, int y, uint32_t color, const char* fmt, ...)
    __attribute__((format(printf, 4, 5)));
void        ui_rect(int x, int y, int w, int h, uint32_t color);
void        ui_hline(int x0, int x1, int y, uint32_t color);
void        ui_vline(int x, int y0, int y1, uint32_t color);
void        ui_line(int x0, int y0, int x1, int y1, uint32_t color);
void        ui_column(int x, int y_top, const uint32_t* colors, int count);
void        ui_draw_frame(view_t active, const char* hints);
uint32_t    ui_heat_color(float level);
bool        ui_shift(const ui_key_t* key);
void        ui_format_ago(char* out, size_t size, int64_t then_us, int64_t now_us);
