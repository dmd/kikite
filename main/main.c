#include <stdio.h>
#include <string.h>
#include "app.h"
#include "bsp/device.h"
#include "bsp/display.h"
#include "bsp/input.h"
#include "capture.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "nvs.h"
#include "nvs_flash.h"
#include "pax_gfx.h"
#include "spectrum.h"
#include "telemetry.h"
#include "wifi_connection.h"
#include "wifi_remote.h"

static const char TAG[] = "kikite";

#define REDRAW_INTERVAL_US (100 * 1000)

static pax_buf_t     fb;
static size_t        display_h_res;
static size_t        display_v_res;
static QueueHandle_t input_queue;
static QueueHandle_t remote_key_queue;
static view_t        current_view       = VIEW_SPECTRUM;
static view_t        radio_owner_view   = VIEW_SPECTRUM;
static region_t      region             = REGION_US915;
static bool          region_switchable  = true;
static bool          radio_ready        = false;
static char          startup_error[96]  = "";
static volatile bool display_wanted     = true;
static volatile bool screenshot_wanted  = false;
static bool          display_on         = true;
static uint8_t       saved_brightness   = 100;

static void blit(void) {
    bsp_display_blit(0, 0, display_h_res, display_v_res, pax_buf_get_pixels(&fb));
}

static void show_message(const char* line1, const char* line2) {
    pax_background(&fb, COLOR_BG);
    ui_text(24, 200, COLOR_ACCENT, "kikite");
    ui_text(24, 230, COLOR_TEXT, "%s", line1);
    if (line2) {
        ui_text(24, 254, COLOR_DIM, "%s", line2);
    }
    blit();
}

static bool init_display(void) {
    bsp_display_color_format_t color_format;
    bsp_display_endianness_t   endianness;
    if (bsp_display_get_parameters(&display_h_res, &display_v_res, &color_format, &endianness) != ESP_OK) {
        return false;
    }
    if (color_format != BSP_DISPLAY_COLOR_FORMAT_24_888RGB) {
        ESP_LOGW(TAG, "Unexpected display color format %d", color_format);
    }
    pax_buf_init(&fb, NULL, display_h_res, display_v_res, PAX_BUF_24_888RGB);
    pax_buf_reversed(&fb, endianness == BSP_DISPLAY_ENDIAN_BIG);

    pax_orientation_t orientation = PAX_O_UPRIGHT;
    switch (bsp_display_get_default_rotation()) {
        case BSP_DISPLAY_ROTATION_90:
            orientation = PAX_O_ROT_CCW;
            break;
        case BSP_DISPLAY_ROTATION_180:
            orientation = PAX_O_ROT_HALF;
            break;
        case BSP_DISPLAY_ROTATION_270:
            orientation = PAX_O_ROT_CW;
            break;
        default:
            break;
    }
    pax_buf_set_orientation(&fb, orientation);
    ui_init(&fb, endianness == BSP_DISPLAY_ENDIAN_BIG);
    return true;
}

static void load_region(void) {
    nvs_handle_t handle;
    if (nvs_open("kikite", NVS_READONLY, &handle) == ESP_OK) {
        uint8_t stored = REGION_US915;
        if (nvs_get_u8(handle, "region", &stored) == ESP_OK && stored < REGION_433) {
            region = stored;
        }
        nvs_close(handle);
    }
}

static void save_region(void) {
    nvs_handle_t handle;
    if (nvs_open("kikite", NVS_READWRITE, &handle) == ESP_OK) {
        nvs_set_u8(handle, "region", region);
        nvs_commit(handle);
        nvs_close(handle);
    }
}

region_t app_region(void) {
    return region;
}

bool app_region_switchable(void) {
    return region_switchable;
}

void app_set_region(region_t new_region) {
    if (!region_switchable || new_region == region || new_region >= REGION_433) {
        return;
    }
    region = new_region;
    save_region();
    view_spectrum_region_changed();
    view_listen_region_changed();
    app_radio_activity_changed();
}

void app_radio_activity_changed(void) {
    if (!radio_ready) {
        return;
    }
    if (radio_owner_view == VIEW_LISTEN) {
        view_listen_activate();
    } else {
        view_spectrum_activate();
    }
}

void app_show(view_t view) {
    current_view = view;
    if (view == VIEW_SPECTRUM || view == VIEW_LISTEN) {
        radio_owner_view = view;
        app_radio_activity_changed();
    }
}

void app_request_screenshot(void) {
    screenshot_wanted = true;
}

void app_request_display(bool on) {
    display_wanted = on;
}

static void apply_display_request(void) {
    if (display_wanted == display_on) {
        return;
    }
    display_on = display_wanted;
    if (display_on) {
        bsp_display_set_backlight_brightness(saved_brightness);
    } else {
        bsp_display_get_backlight_brightness(&saved_brightness);
        bsp_display_set_backlight_brightness(0);
    }
}

void app_listen_at(uint32_t frequency_hz) {
    view_listen_set_cursor_frequency(frequency_hz);
    app_show(VIEW_LISTEN);
}

static void exit_to_launcher(void) {
    display_wanted = true;
    apply_display_request();
    show_message("Restoring radio settings...", NULL);
    if (radio_ready) {
        radio_restore_and_stop();
    }
    bsp_device_restart_to_launcher();
}

static void draw(void) {
    pax_background(&fb, COLOR_BG);
    if (startup_error[0] != '\0') {
        ui_draw_frame(current_view, "F1 exit");
        ui_text(24, 120, COLOR_BAD, "Radio unavailable");
        ui_text(24, 150, COLOR_TEXT, "%s", startup_error);
        ui_text(24, 180, COLOR_DIM, "Check for a radio firmware update in the launcher.");
        blit();
        return;
    }
    switch (current_view) {
        case VIEW_SPECTRUM:
            view_spectrum_draw();
            break;
        case VIEW_LISTEN:
            view_listen_draw();
            break;
        case VIEW_DEVICES:
            view_devices_draw();
            break;
        case VIEW_RADIO:
        default:
            view_radio_draw();
            break;
    }
    blit();
}

static bool translate_event(const bsp_input_event_t* event, ui_key_t* key) {
    memset(key, 0, sizeof(*key));
    if (event->type == INPUT_EVENT_TYPE_NAVIGATION) {
        if (!event->args_navigation.state) {
            return false;
        }
        switch (event->args_navigation.key) {
            case BSP_INPUT_NAVIGATION_KEY_SPACE_L:
            case BSP_INPUT_NAVIGATION_KEY_SPACE_M:
            case BSP_INPUT_NAVIGATION_KEY_SPACE_R:
            case BSP_INPUT_NAVIGATION_KEY_BACKSPACE:
            case BSP_INPUT_NAVIGATION_KEY_TAB:
                return false;
            default:
                break;
        }
        key->is_navigation = true;
        key->key           = event->args_navigation.key;
        key->modifiers     = event->args_navigation.modifiers;
        return true;
    }
    if (event->type == INPUT_EVENT_TYPE_KEYBOARD) {
        key->ascii     = event->args_keyboard.ascii;
        key->modifiers = event->args_keyboard.modifiers;
        return key->ascii != '\0';
    }
    return false;
}

static void handle_key(const ui_key_t* key) {
    if (key->is_navigation) {
        switch (key->key) {
            case BSP_INPUT_NAVIGATION_KEY_F1:
                exit_to_launcher();
                return;
            case BSP_INPUT_NAVIGATION_KEY_F2:
                app_show(VIEW_SPECTRUM);
                return;
            case BSP_INPUT_NAVIGATION_KEY_F3:
                app_show(VIEW_LISTEN);
                return;
            case BSP_INPUT_NAVIGATION_KEY_F4:
                app_show(VIEW_DEVICES);
                return;
            case BSP_INPUT_NAVIGATION_KEY_F5:
                app_show(VIEW_RADIO);
                return;
            default:
                break;
        }
    } else if (key->ascii >= '1' && key->ascii <= '0' + VIEW_COUNT) {
        app_show((view_t)(key->ascii - '1'));
        return;
    }

    if (!radio_ready) {
        return;
    }
    switch (current_view) {
        case VIEW_SPECTRUM:
            view_spectrum_key(key);
            break;
        case VIEW_LISTEN:
            view_listen_key(key);
            break;
        case VIEW_DEVICES:
            view_devices_key(key);
            break;
        case VIEW_RADIO:
            view_radio_key(key);
            break;
        default:
            break;
    }
}

static bool start_radio(void) {
    if (wifi_remote_initialize() != ESP_OK) {
        snprintf(startup_error, sizeof(startup_error), "The radio module did not respond.");
        return false;
    }
    wifi_connection_init_stack();
    esp_err_t res = radio_start();
    if (res != ESP_OK) {
        snprintf(startup_error, sizeof(startup_error), "LoRa interface error: %s", esp_err_to_name(res));
        return false;
    }
    radio_info_t info;
    radio_get_info(&info);
    if (info.chip_type == LORA_PROTOCOL_CHIP_SX1268) {
        region            = REGION_433;
        region_switchable = false;
    } else {
        load_region();
    }
    return true;
}

void app_main(void) {
    gpio_install_isr_service(0);

    esp_err_t res = nvs_flash_init();
    if (res == ESP_ERR_NVS_NO_FREE_PAGES || res == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        nvs_flash_erase();
        res = nvs_flash_init();
    }
    if (res != ESP_OK) {
        ESP_LOGE(TAG, "NVS init failed: %s", esp_err_to_name(res));
    }

    const bsp_configuration_t bsp_configuration = {
        .display =
            {
                .requested_color_format = BSP_DISPLAY_COLOR_FORMAT_24_888RGB,
                .num_fbs                = 1,
            },
    };
    ESP_ERROR_CHECK(bsp_device_initialize(&bsp_configuration));
    if (!init_display()) {
        ESP_LOGE(TAG, "Display init failed");
        return;
    }
    ESP_ERROR_CHECK(bsp_input_get_queue(&input_queue));

    show_message("Starting radio...", "Connecting to the LoRa interface on the radio module");

    remote_key_queue = xQueueCreate(32, sizeof(ui_key_t));
    if (!spectrum_init() || !capture_init() || !remote_key_queue || !telemetry_start(remote_key_queue)) {
        snprintf(startup_error, sizeof(startup_error), "Out of memory.");
    } else if (start_radio()) {
        radio_ready = true;
        app_show(VIEW_SPECTRUM);
    }

    int64_t last_draw = 0;
    while (1) {
        bsp_input_event_t event;
        bool              redraw = false;
        if (xQueueReceive(input_queue, &event, pdMS_TO_TICKS(20)) == pdTRUE) {
            ui_key_t key;
            if (translate_event(&event, &key)) {
                handle_key(&key);
                redraw = true;
            }
        }
        ui_key_t remote_key;
        while (xQueueReceive(remote_key_queue, &remote_key, 0) == pdTRUE) {
            handle_key(&remote_key);
            redraw = true;
        }
        apply_display_request();
        int64_t now = esp_timer_get_time();
        if (display_on && (redraw || now - last_draw >= REDRAW_INTERVAL_US)) {
            draw();
            last_draw = now;
            if (screenshot_wanted) {
                screenshot_wanted = false;
                telemetry_framebuffer(pax_buf_get_pixels(&fb), pax_buf_get_width_raw(&fb), pax_buf_get_height_raw(&fb));
            }
        }
    }
}
