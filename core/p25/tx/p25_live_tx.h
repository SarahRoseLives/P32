// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 The RadioFoundry Authors
// Ported from DM32-Multimode for P32 conventional P25 TX.
#ifndef P25_LIVE_TX_H
#define P25_LIVE_TX_H
#include "p25_test_tx.h"
#define LIVE_BANKS 8u
#define LIVE_CAPTURE_FRAMES 1500u
#define LIVE_CALL_MAX_US 33000000u
#define LIVE_RF_MAX_US 35000000u
#define LIVE_STARTUP_BASE_BYTES 240u
#define LIVE_STARTUP_EXTRA_MAX_BYTES (P25_TX_BLOCK_BYTES - 1u)
// Caller holds the IRQ lock for publication, completion, polling and consumption.
typedef struct {
    p25_test_tx timing;
    uint8_t banks[LIVE_BANKS][216];
    uint8_t hdu[99],tdu[18];
    unsigned head, count, pos, high, published, consumed, underruns;
    unsigned header_pos, terminator_pos;
    uint8_t startup_extra;
    bool finished;
} p25_live_tx;
void p25_live_tx_init(p25_live_tx *s);
// Add 0..35 startup fill bytes before the HDU, only while idle. Zero is the
// original 240-byte preamble; no fill is inserted between protocol units.
bool p25_live_tx_set_startup_extra(p25_live_tx *s, unsigned extra);
bool p25_live_tx_push(p25_live_tx *s,const uint8_t ldu[216]);
bool p25_live_tx_finish(p25_live_tx *s);
bool p25_live_tx_start(p25_live_tx *s,uint32_t now);
bool p25_live_tx_boundary(p25_live_tx *s,uint32_t now,uint8_t block[36]);
#endif
