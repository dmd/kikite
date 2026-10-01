#include "spectrum.h"
#include <string.h>
#include "esp_heap_caps.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

static spectrum_t        spectrum;
static SemaphoreHandle_t spectrum_mutex;

static void fill_no_data(int16_t* row, size_t count) {
    for (size_t i = 0; i < count; i++) {
        row[i] = SPECTRUM_NO_DATA;
    }
}

bool spectrum_init(void) {
    spectrum_mutex   = xSemaphoreCreateMutex();
    spectrum.live     = heap_caps_malloc(SPECTRUM_MAX_BINS * sizeof(int16_t), MALLOC_CAP_SPIRAM);
    spectrum.max_hold = heap_caps_malloc(SPECTRUM_MAX_BINS * sizeof(int16_t), MALLOC_CAP_SPIRAM);
    spectrum.history =
        heap_caps_malloc((size_t)SPECTRUM_HISTORY_ROWS * SPECTRUM_MAX_BINS * sizeof(int16_t), MALLOC_CAP_SPIRAM);
    if (!spectrum_mutex || !spectrum.live || !spectrum.max_hold || !spectrum.history) {
        return false;
    }
    fill_no_data(spectrum.live, SPECTRUM_MAX_BINS);
    fill_no_data(spectrum.max_hold, SPECTRUM_MAX_BINS);
    fill_no_data(spectrum.history, (size_t)SPECTRUM_HISTORY_ROWS * SPECTRUM_MAX_BINS);
    return true;
}

void spectrum_begin_sweep(uint32_t start_hz, uint32_t step_hz, uint16_t bins) {
    if (bins > SPECTRUM_MAX_BINS) {
        bins = SPECTRUM_MAX_BINS;
    }
    xSemaphoreTake(spectrum_mutex, portMAX_DELAY);
    if (spectrum.start_hz != start_hz || spectrum.step_hz != step_hz || spectrum.bins != bins) {
        spectrum.start_hz         = start_hz;
        spectrum.step_hz          = step_hz;
        spectrum.bins             = bins;
        spectrum.completed_sweeps = 0;
        spectrum.history_head     = 0;
        spectrum.history_count    = 0;
        fill_no_data(spectrum.max_hold, SPECTRUM_MAX_BINS);
        fill_no_data(spectrum.history, (size_t)SPECTRUM_HISTORY_ROWS * SPECTRUM_MAX_BINS);
    }
    fill_no_data(spectrum.live, SPECTRUM_MAX_BINS);
    spectrum.progress_bin = 0;
    xSemaphoreGive(spectrum_mutex);
}

void spectrum_store_bin(uint16_t bin, int16_t dbm_x2) {
    xSemaphoreTake(spectrum_mutex, portMAX_DELAY);
    if (bin < spectrum.bins) {
        spectrum.live[bin] = dbm_x2;
        if (dbm_x2 != SPECTRUM_NO_DATA &&
            (spectrum.max_hold[bin] == SPECTRUM_NO_DATA || dbm_x2 > spectrum.max_hold[bin])) {
            spectrum.max_hold[bin] = dbm_x2;
        }
        spectrum.progress_bin = bin + 1;
    }
    xSemaphoreGive(spectrum_mutex);
}

void spectrum_end_sweep(void) {
    xSemaphoreTake(spectrum_mutex, portMAX_DELAY);
    memcpy(&spectrum.history[(size_t)spectrum.history_head * SPECTRUM_MAX_BINS], spectrum.live,
           SPECTRUM_MAX_BINS * sizeof(int16_t));
    spectrum.history_head = (spectrum.history_head + 1) % SPECTRUM_HISTORY_ROWS;
    if (spectrum.history_count < SPECTRUM_HISTORY_ROWS) {
        spectrum.history_count++;
    }
    spectrum.completed_sweeps++;
    xSemaphoreGive(spectrum_mutex);
}

void spectrum_reset_max_hold(void) {
    xSemaphoreTake(spectrum_mutex, portMAX_DELAY);
    fill_no_data(spectrum.max_hold, SPECTRUM_MAX_BINS);
    xSemaphoreGive(spectrum_mutex);
}

const spectrum_t* spectrum_lock(void) {
    xSemaphoreTake(spectrum_mutex, portMAX_DELAY);
    return &spectrum;
}

void spectrum_unlock(void) {
    xSemaphoreGive(spectrum_mutex);
}

const int16_t* spectrum_history_row(const spectrum_t* s, uint16_t age) {
    if (age >= s->history_count) {
        return NULL;
    }
    uint16_t row = (s->history_head + SPECTRUM_HISTORY_ROWS - 1 - age) % SPECTRUM_HISTORY_ROWS;
    return &s->history[(size_t)row * SPECTRUM_MAX_BINS];
}
