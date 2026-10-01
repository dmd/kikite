#include <stdio.h>
#include <string.h>
#include "app.h"
#include "esp_heap_caps.h"
#include "spectrum.h"

#define PLOT_X       64
#define PLOT_W       (UI_WIDTH - PLOT_X - 8)
#define INFO_Y       (UI_CONTENT_TOP + 2)
#define TICK_Y       (INFO_Y + 2 * UI_LINE_H + 2)
#define PLOT_TOP     (TICK_Y + UI_LINE_H + 2)
#define PLOT_H       150
#define PLOT_SPAN    80
#define READOUT_Y    (PLOT_TOP + PLOT_H + 6)
#define WF_TOP       (READOUT_Y + UI_LINE_H + 4)
#define WF_H         (UI_CONTENT_BOT - WF_TOP)
#define WF_RANGE_DB  45.0f
#define HEAT_LEVELS  256

static const uint16_t bandwidths_khz[] = {62, 125, 250, 500};
static const uint8_t  sample_options[] = {1, 2, 4, 8};
static uint8_t        range_index      = 0;
static uint8_t        bandwidth_index  = 1;
static uint8_t        samples_index    = 1;
static sweep_method_t method           = SWEEP_RETUNE_THEN_RX;
static bool           paused           = false;
static int            cursor_bin       = -1;
static int            reference_dbm    = -50;

typedef struct {
    uint32_t start_hz;
    uint32_t step_hz;
    uint16_t bins;
    uint32_t completed_sweeps;
    uint16_t progress_bin;
    bool     has_last;
    int16_t  live[SPECTRUM_MAX_BINS];
    int16_t  max_hold[SPECTRUM_MAX_BINS];
    int16_t  last[SPECTRUM_MAX_BINS];
} snapshot_t;

static snapshot_t* snapshot;
static uint32_t*   waterfall;
static uint16_t    waterfall_head;
static uint16_t    waterfall_rows;
static uint32_t    waterfall_sweeps;
static float       waterfall_floor = -120.0f;
static uint32_t    waterfall_start_hz;
static uint32_t    waterfall_step_hz;
static uint16_t    waterfall_bins;
static uint16_t    column_first[PLOT_W];
static uint16_t    column_last[PLOT_W];
static uint32_t    heat[HEAT_LEVELS];
static bool        ready;

static bool ensure_buffers(void) {
    if (ready) {
        return true;
    }
    if (!snapshot) {
        snapshot = heap_caps_calloc(1, sizeof(snapshot_t), MALLOC_CAP_SPIRAM);
    }
    if (!waterfall) {
        waterfall = heap_caps_calloc((size_t)PLOT_W * WF_H, sizeof(uint32_t), MALLOC_CAP_SPIRAM);
    }
    if (!snapshot || !waterfall) {
        return false;
    }
    for (int level = 0; level < HEAT_LEVELS; level++) {
        heat[level] = ui_heat_color(level / (float)(HEAT_LEVELS - 1));
    }
    ready = true;
    return true;
}

static uint32_t step_hz(void) {
    uint16_t bandwidth = bandwidths_khz[bandwidth_index];
    return bandwidth == 62 ? 62500 : (uint32_t)bandwidth * 1000;
}

static sweep_params_t current_params(void) {
    uint8_t              count  = 0;
    const sweep_range_t* ranges = profiles_sweep_ranges(app_region(), &count);
    const sweep_range_t* range  = &ranges[range_index % count];
    uint32_t             step   = step_hz();
    uint32_t             bins   = (range->stop_hz - range->start_hz) / step + 1;
    if (bins > SPECTRUM_MAX_BINS) {
        bins = SPECTRUM_MAX_BINS;
    }
    sweep_params_t params = {
        .start_hz         = range->start_hz,
        .step_hz          = step,
        .bins             = bins,
        .bandwidth_khz    = bandwidths_khz[bandwidth_index],
        .samples_per_step = sample_options[samples_index],
        .settle_us        = 0,
        .method           = method,
    };
    return params;
}

static void apply(void) {
    sweep_params_t params = current_params();
    if (cursor_bin < 0 || cursor_bin >= params.bins) {
        cursor_bin = params.bins / 2;
    }
    if (paused) {
        radio_request_idle();
    } else {
        radio_request_sweep(&params);
    }
}

void view_spectrum_activate(void) {
    apply();
}

void view_spectrum_region_changed(void) {
    range_index = 0;
    cursor_bin  = -1;
}

static void build_column_table(uint16_t bins) {
    for (int column = 0; column < PLOT_W; column++) {
        int first = (int)((int64_t)column * bins / PLOT_W);
        int last  = (int)((int64_t)(column + 1) * bins / PLOT_W);
        if (last <= first) {
            last = first + 1;
        }
        if (last > bins) {
            last = bins;
        }
        column_first[column] = first;
        column_last[column]  = last;
    }
}

static int16_t column_peak(const int16_t* row, int column) {
    int16_t peak = SPECTRUM_NO_DATA;
    for (int bin = column_first[column]; bin < column_last[column]; bin++) {
        if (row[bin] > peak) {
            peak = row[bin];
        }
    }
    return peak;
}

static int16_t noise_floor_x2(const int16_t* row, uint16_t bins) {
    static uint16_t histogram[301];
    memset(histogram, 0, sizeof(histogram));
    int valid = 0;
    for (int bin = 0; bin < bins; bin++) {
        if (row[bin] == SPECTRUM_NO_DATA) {
            continue;
        }
        int index = -row[bin];
        if (index < 0) {
            index = 0;
        }
        if (index > 300) {
            index = 300;
        }
        histogram[index]++;
        valid++;
    }
    if (valid == 0) {
        return -240;
    }
    int target = valid * 4 / 5 > 0 ? valid * 4 / 5 : 1;
    int seen   = 0;
    for (int index = 0; index <= 300; index++) {
        seen += histogram[index];
        if (seen >= target) {
            return -index;
        }
    }
    return -240;
}

static void add_waterfall_row(const int16_t* row, uint16_t bins) {
    waterfall_floor = noise_floor_x2(row, bins) / 2.0f - 3.0f;
    uint32_t* line  = &waterfall[(size_t)waterfall_head * PLOT_W];
    for (int column = 0; column < PLOT_W; column++) {
        int16_t value = column_peak(row, column);
        if (value == SPECTRUM_NO_DATA) {
            line[column] = COLOR_PANEL;
            continue;
        }
        float level = (value / 2.0f - waterfall_floor) / WF_RANGE_DB;
        int   index = (int)(level * (HEAT_LEVELS - 1) + 0.5f);
        if (index < 0) {
            index = 0;
        }
        if (index >= HEAT_LEVELS) {
            index = HEAT_LEVELS - 1;
        }
        line[column] = heat[index];
    }
    waterfall_head = (waterfall_head + 1) % WF_H;
    if (waterfall_rows < WF_H) {
        waterfall_rows++;
    }
}

static void take_snapshot(void) {
    const spectrum_t* s = spectrum_lock();
    snapshot->start_hz         = s->start_hz;
    snapshot->step_hz          = s->step_hz;
    snapshot->bins             = s->bins;
    snapshot->completed_sweeps = s->completed_sweeps;
    snapshot->progress_bin     = s->progress_bin;
    memcpy(snapshot->live, s->live, s->bins * sizeof(int16_t));
    memcpy(snapshot->max_hold, s->max_hold, s->bins * sizeof(int16_t));
    const int16_t* last = spectrum_history_row(s, 0);
    snapshot->has_last  = last != NULL;
    if (last) {
        memcpy(snapshot->last, last, s->bins * sizeof(int16_t));
    }

    bool geometry_changed = s->start_hz != waterfall_start_hz || s->step_hz != waterfall_step_hz ||
                            s->bins != waterfall_bins || s->completed_sweeps < waterfall_sweeps;
    if (geometry_changed) {
        waterfall_start_hz = s->start_hz;
        waterfall_step_hz  = s->step_hz;
        waterfall_bins     = s->bins;
        waterfall_sweeps   = 0;
        waterfall_head     = 0;
        waterfall_rows     = 0;
        build_column_table(s->bins ? s->bins : 1);
    }
    uint32_t pending = s->completed_sweeps - waterfall_sweeps;
    if (pending > s->history_count) {
        pending = s->history_count;
    }
    if (pending > WF_H) {
        pending = WF_H;
    }
    for (int age = (int)pending - 1; age >= 0; age--) {
        add_waterfall_row(spectrum_history_row(s, age), s->bins);
    }
    waterfall_sweeps = s->completed_sweeps;
    spectrum_unlock();
}

static int dbm_to_y(float dbm) {
    float y = PLOT_TOP + (reference_dbm - dbm) * PLOT_H / PLOT_SPAN;
    if (y < PLOT_TOP) {
        y = PLOT_TOP;
    }
    if (y > PLOT_TOP + PLOT_H - 1) {
        y = PLOT_TOP + PLOT_H - 1;
    }
    return (int)y;
}

static int bin_to_x(int bin, uint16_t bins) {
    return PLOT_X + (int)(((int64_t)bin * PLOT_W + PLOT_W / 2) / bins);
}

static void draw_trace(const int16_t* row, uint32_t color, int limit_bin) {
    int previous_x = -1;
    int previous_y = 0;
    for (int column = 0; column < PLOT_W; column++) {
        if (column_first[column] >= limit_bin) {
            break;
        }
        int16_t value = column_peak(row, column);
        if (value == SPECTRUM_NO_DATA) {
            previous_x = -1;
            continue;
        }
        int x = PLOT_X + column;
        int y = dbm_to_y(value / 2.0f);
        if (previous_x >= 0) {
            ui_line(previous_x, previous_y, x, y, color);
        } else {
            ui_vline(x, y, y, color);
        }
        previous_x = x;
        previous_y = y;
    }
}

static void draw_waterfall(void) {
    static uint32_t column_colors[WF_H];
    for (int column = 0; column < PLOT_W; column++) {
        for (int age = 0; age < WF_H; age++) {
            if (age >= waterfall_rows) {
                column_colors[age] = COLOR_BG;
                continue;
            }
            int slot           = (waterfall_head + WF_H - 1 - age) % WF_H;
            column_colors[age] = waterfall[(size_t)slot * PLOT_W + column];
        }
        ui_column(PLOT_X + column, WF_TOP, column_colors, WF_H);
    }
    ui_text_right(PLOT_X - 4, WF_TOP, COLOR_DIM, "%d", (int)(waterfall_floor + WF_RANGE_DB));
    ui_text_right(PLOT_X - 4, WF_TOP + WF_H - UI_LINE_H, COLOR_DIM, "%d", (int)waterfall_floor);
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
        default:
            return "500";
    }
}

static void draw_header(const sweep_params_t* params, const radio_info_t* info) {
    uint8_t     range_count;
    const char* range_name = profiles_sweep_ranges(app_region(), &range_count)[range_index % range_count].name;
    ui_text(8, INFO_Y, COLOR_ACCENT, "%s", range_name);
    if (info->last_sweep_ms) {
        ui_text_right(UI_WIDTH - 8, INFO_Y, COLOR_DIM, "%.1f s/sweep  %.1f ms/step", info->last_sweep_ms / 1000.0f,
                      info->timing.step_us / 1000.0f);
    }
    uint32_t stop_hz = params->start_hz + (params->bins - 1) * params->step_hz;
    char     stop[16];
    format_mhz(stop, sizeof(stop), stop_hz);
    ui_text(8, INFO_Y + UI_LINE_H, paused ? COLOR_WARN : COLOR_DIM, "BW %sk  %u bins to %s  avg %u  %s",
            bandwidth_text(params->bandwidth_khz), params->bins, stop, params->samples_per_step,
            paused ? "PAUSED" : radio_sweep_method_name(method));

    for (int dbm = reference_dbm; dbm >= reference_dbm - PLOT_SPAN; dbm -= 10) {
        int y = dbm_to_y(dbm);
        ui_hline(PLOT_X, PLOT_X + PLOT_W - 1, y, COLOR_GRID);
        if ((reference_dbm - dbm) % 20 == 0) {
            ui_text_right(PLOT_X - 4, y - 9, COLOR_DIM, "%d", dbm);
        }
    }
}

static void draw_spectrum(void) {
    radio_info_t info;
    radio_get_info(&info);
    sweep_params_t params = current_params();
    draw_header(&params, &info);

    if (!ensure_buffers()) {
        ui_text(PLOT_X + 8, PLOT_TOP + 8, COLOR_BAD, "Out of memory");
        return;
    }
    if (info.rssi_unsupported) {
        ui_text(PLOT_X + 8, PLOT_TOP + 10, COLOR_BAD, "The radio does not answer signal-strength");
        ui_text(PLOT_X + 8, PLOT_TOP + 30, COLOR_BAD, "requests. Update the radio firmware from");
        ui_text(PLOT_X + 8, PLOT_TOP + 50, COLOR_BAD, "the launcher's settings.");
        return;
    }

    take_snapshot();
    snapshot_t* s = snapshot;
    if (s->bins == 0 || s->bins != params.bins || s->start_hz != params.start_hz || s->step_hz != params.step_hz) {
        ui_text(PLOT_X + 8, PLOT_TOP + 8, COLOR_DIM, paused ? "Paused" : "Starting sweep...");
        return;
    }

    for (int tick = 0; tick <= 4; tick++) {
        int  bin = (s->bins - 1) * tick / 4;
        int  x   = bin_to_x(bin, s->bins);
        char label[16];
        format_mhz(label, sizeof(label), s->start_hz + bin * s->step_hz);
        ui_vline(x, PLOT_TOP, PLOT_TOP + PLOT_H - 1, COLOR_GRID);
        if (tick == 0) {
            ui_text(x, TICK_Y, COLOR_DIM, "%s", label);
        } else if (tick == 4) {
            ui_text_right(x, TICK_Y, COLOR_DIM, "%s", label);
        } else {
            ui_text(x - (int)strlen(label) * UI_CHAR_W / 2, TICK_Y, COLOR_DIM, "%s", label);
        }
    }

    bool sweeping = s->progress_bin < s->bins;
    draw_trace(s->max_hold, COLOR_MAXHOLD, s->bins);
    if (sweeping && s->has_last) {
        draw_trace(s->last, COLOR_DIM, s->bins);
    }
    draw_trace(s->live, COLOR_TRACE, s->progress_bin);
    if (sweeping && !paused) {
        int x = bin_to_x(s->progress_bin, s->bins);
        ui_vline(x, PLOT_TOP + PLOT_H - 6, PLOT_TOP + PLOT_H - 1, COLOR_ACCENT);
    }

    draw_waterfall();

    if (cursor_bin >= s->bins) {
        cursor_bin = s->bins - 1;
    }
    int cursor_x = bin_to_x(cursor_bin, s->bins);
    ui_vline(cursor_x, PLOT_TOP, PLOT_TOP + PLOT_H - 1, COLOR_ACCENT);
    ui_vline(cursor_x, WF_TOP - 4, WF_TOP - 1, COLOR_ACCENT);

    char frequency[16];
    format_mhz(frequency, sizeof(frequency), s->start_hz + cursor_bin * s->step_hz);
    int16_t live = cursor_bin < s->progress_bin ? s->live[cursor_bin] : SPECTRUM_NO_DATA;
    if (live == SPECTRUM_NO_DATA && s->has_last) {
        live = s->last[cursor_bin];
    }
    int16_t held = s->max_hold[cursor_bin];
    char    live_text[16];
    char    held_text[16];
    snprintf(live_text, sizeof(live_text), live == SPECTRUM_NO_DATA ? "--" : "%.1f", live / 2.0f);
    snprintf(held_text, sizeof(held_text), held == SPECTRUM_NO_DATA ? "--" : "%.1f", held / 2.0f);

    ui_text(8, READOUT_Y, COLOR_ACCENT, "%s MHz", frequency);
    ui_text(8 + 16 * UI_CHAR_W, READOUT_Y, COLOR_TRACE, "now %s", live_text);
    ui_text(8 + 27 * UI_CHAR_W, READOUT_Y, COLOR_MAXHOLD, "max %s dBm", held_text);
}

void view_spectrum_draw(void) {
    draw_spectrum();
    ui_draw_frame(VIEW_SPECTRUM, "<> cursor  ^v ref  R range  B bw  S avg  H hold  L listen  Spc pause");
}

void view_spectrum_key(const ui_key_t* key) {
    sweep_params_t params = current_params();
    if (key->is_navigation) {
        int stride = ui_shift(key) ? 10 : 1;
        switch (key->key) {
            case BSP_INPUT_NAVIGATION_KEY_LEFT:
                cursor_bin = cursor_bin - stride < 0 ? 0 : cursor_bin - stride;
                break;
            case BSP_INPUT_NAVIGATION_KEY_RIGHT:
                cursor_bin = cursor_bin + stride >= params.bins ? params.bins - 1 : cursor_bin + stride;
                break;
            case BSP_INPUT_NAVIGATION_KEY_UP:
                reference_dbm = reference_dbm < 0 ? reference_dbm + 10 : reference_dbm;
                break;
            case BSP_INPUT_NAVIGATION_KEY_DOWN:
                reference_dbm = reference_dbm > -80 ? reference_dbm - 10 : reference_dbm;
                break;
            default:
                break;
        }
        return;
    }

    uint8_t range_count;
    profiles_sweep_ranges(app_region(), &range_count);
    switch (key->ascii) {
        case 'r':
            range_index = (range_index + 1) % range_count;
            cursor_bin  = -1;
            apply();
            break;
        case 'R':
            range_index = (range_index + range_count - 1) % range_count;
            cursor_bin  = -1;
            apply();
            break;
        case 'b':
        case 'B': {
            uint32_t cursor_hz = params.start_hz + cursor_bin * params.step_hz;
            bandwidth_index    = (bandwidth_index + 1) % (sizeof(bandwidths_khz) / sizeof(bandwidths_khz[0]));
            sweep_params_t next = current_params();
            cursor_bin          = (cursor_hz - next.start_hz) / next.step_hz;
            apply();
            break;
        }
        case 's':
        case 'S':
            samples_index = (samples_index + 1) % sizeof(sample_options);
            apply();
            break;
        case 'm':
        case 'M':
            method = (method + 1) % SWEEP_METHOD_COUNT;
            apply();
            break;
        case 'h':
        case 'H':
            spectrum_reset_max_hold();
            break;
        case ' ':
            paused = !paused;
            apply();
            break;
        case 'l':
        case 'L':
            app_listen_at(params.start_hz + cursor_bin * params.step_hz);
            break;
        default:
            break;
    }
}
