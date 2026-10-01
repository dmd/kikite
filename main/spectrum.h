#pragma once

#include <stdbool.h>
#include <stdint.h>

#define SPECTRUM_MAX_BINS     1400
#define SPECTRUM_HISTORY_ROWS 256
#define SPECTRUM_NO_DATA      INT16_MIN

typedef struct {
    uint32_t start_hz;
    uint32_t step_hz;
    uint16_t bins;
    uint32_t completed_sweeps;
    uint16_t progress_bin;
    int16_t* live;
    int16_t* max_hold;
    int16_t* history;
    uint16_t history_head;
    uint16_t history_count;
} spectrum_t;

bool             spectrum_init(void);
void             spectrum_begin_sweep(uint32_t start_hz, uint32_t step_hz, uint16_t bins);
void             spectrum_store_bin(uint16_t bin, int16_t dbm_x2);
void             spectrum_end_sweep(void);
void             spectrum_reset_max_hold(void);
const spectrum_t* spectrum_lock(void);
void             spectrum_unlock(void);
const int16_t*   spectrum_history_row(const spectrum_t* spectrum, uint16_t age);
