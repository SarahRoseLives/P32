// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef P25_DEMOD_H
#define P25_DEMOD_H
#include "p25_rx.h"
// Bounded 8-kHz discriminator input. Ten timing hypotheses at 48 kHz.
// Sync acquires polarity, DC and gain; decision-directed timing tracks each
// hypothesis between syncs. No whole-call search, heap, or floating point.
typedef struct {
    p25_rx rx;
    int16_t history[48]; // duplicated, quantized sync history
    unsigned index, filled, quality;
    int32_t offset, gain;
    int32_t next_q16;
    int32_t sum, sum_squares;
    int32_t period_q16, previous_value, previous_symbol;
} p25_demod_lane;
typedef struct {
    p25_demod_lane lane[10];
    int16_t fir[42];
    unsigned fir_index, pending_ticks, pending_quality, pending_errors;
    p25_rx pending;
    uint32_t frames, acquisitions;
    uint64_t ticks;
    uint32_t (*clock_us)(void);
    uint32_t fir_us, sync_us, fec_us, symbol_us;
    uint32_t sync_tests, fec_candidates, fec_skipped;
    int16_t previous;
    // Retain the last five milliseconds at 48 kHz, quantized exactly like
    // the sync histories. Idle rephasing must preserve an arriving sync.
    int16_t hunt_history[240];
    uint16_t hunt_index,hunt_filled,idle_ticks;
    uint32_t hunt_rearms,hunt_us,hunt_max_us;
    // Measure the rare rearm without enabling per-symbol profiling overhead.
    uint32_t (*hunt_clock_us)(void);
} p25_demod;
typedef void (*p25_demod_frame_fn)(const p25_rx *frame,void *context);
void p25_demod_init(p25_demod *d);
void p25_demod_gap(p25_demod *d);
void p25_demod_push(p25_demod *d,int16_t sample,p25_demod_frame_fn emit,void *context);
#endif
