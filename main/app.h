#pragma once

#include <stdint.h>
#include "profiles.h"
#include "radio.h"
#include "ui.h"

region_t app_region(void);
void     app_set_region(region_t region);
bool     app_region_switchable(void);
void     app_show(view_t view);
void     app_listen_at(uint32_t frequency_hz);
void     app_radio_activity_changed(void);
void     app_request_display(bool on);
void     app_request_screenshot(void);

void view_spectrum_activate(void);
void view_spectrum_draw(void);
void view_spectrum_key(const ui_key_t* key);
void view_spectrum_region_changed(void);

void view_listen_activate(void);
void view_listen_draw(void);
void view_listen_key(const ui_key_t* key);
void view_listen_region_changed(void);
void view_listen_set_cursor_frequency(uint32_t frequency_hz);

void view_devices_draw(void);
void view_devices_key(const ui_key_t* key);

void view_radio_draw(void);
void view_radio_key(const ui_key_t* key);
