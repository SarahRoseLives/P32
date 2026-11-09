// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (c) 2026 DM32 P25 research contributors
// Copyright (c) 2026 The RadioFoundry Authors
// Ported from DM32-Multimode for P32 conventional P25 TX.
#ifndef P25_TEST_TX_H
#define P25_TEST_TX_H
#include <stdbool.h>
#include <stdint.h>

#define P25_TX_BLOCK_BYTES 36u
#define P25_TX_MAX_US 12000000u
#define P25_TX_STALL_US 200000u
#define P25_TX_LATE_US 33000u
#define P25_TX_DRAIN_US 60000u

typedef enum {
    P25_TX_IDLE, P25_TX_WAITING, P25_TX_STREAMING, P25_TX_DRAINING,
    P25_TX_DONE, P25_TX_ABORTED
} p25_tx_state;
typedef enum {
    P25_TX_OK, P25_TX_NOT_ARMED, P25_TX_RELEASED, P25_TX_LATE,
    P25_TX_STALLED, P25_TX_TIMEOUT, P25_TX_CANCELLED
} p25_tx_reason;
typedef struct {
    p25_tx_state state;
    p25_tx_reason reason;
    uint32_t started_us, last_boundary_us, drain_until_us;
    uint32_t offset, blocks, duplicate_irqs, late_irqs, max_gap_us;
    bool have_boundary;
} p25_test_tx;

uint32_t p25_test_tx_stream_bytes(void);
void p25_test_tx_init(p25_test_tx *tx);
bool p25_test_tx_start(p25_test_tx *tx, uint32_t now_us, bool armed);
bool p25_test_tx_active(const p25_test_tx *tx);
void p25_test_tx_abort(p25_test_tx *tx, p25_tx_reason reason);
void p25_test_tx_poll(p25_test_tx *tx, uint32_t now_us, bool ptt_down);
void p25_test_tx_poll_limit(p25_test_tx *tx, uint32_t now_us, bool ptt_down, uint32_t limit_us);
// True: one complete physical block, including an explicit post-TDU tail only.
bool p25_test_tx_boundary(p25_test_tx *tx, uint32_t now_us, uint8_t block[P25_TX_BLOCK_BYTES]);
#endif
