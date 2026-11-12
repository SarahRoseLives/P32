// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (c) 2026 The P25 research contributors
// Copyright (c) 2026 The RadioFoundry Authors
// Ported from a GPL-3.0-or-later P25 reference implementation for conventional P25 TX.
#include "p25_test_tx.h"
#include "p25_test_vectors.h"
#include <string.h>

uint32_t p25_test_tx_stream_bytes(void)
{
    return P25_TEST_PREAMBLE_BYTES + sizeof(p25_test_hdu)
        + P25_TEST_PAIR_REPEATS * (sizeof(p25_test_ldu1) + sizeof(p25_test_ldu2))
        + sizeof(p25_test_tdu);
}

static uint8_t stream_byte(uint32_t index)
{
    if (index < P25_TEST_PREAMBLE_BYTES) return 0x77;
    index -= P25_TEST_PREAMBLE_BYTES;
    if (index < sizeof(p25_test_hdu)) return p25_test_hdu[index];
    index -= sizeof(p25_test_hdu);
    const uint32_t pair_bytes = sizeof(p25_test_ldu1) + sizeof(p25_test_ldu2);
    if (index < P25_TEST_PAIR_REPEATS * pair_bytes) {
        index %= pair_bytes;
        if (index < sizeof(p25_test_ldu1)) return p25_test_ldu1[index];
        return p25_test_ldu2[index - sizeof(p25_test_ldu1)];
    }
    index -= P25_TEST_PAIR_REPEATS * pair_bytes;
    if (index < sizeof(p25_test_tdu)) return p25_test_tdu[index];
    return 0x77; // Explicit physical tail after the terminator, never between units.
}

void p25_test_tx_init(p25_test_tx *tx)
{
    memset(tx, 0, sizeof(*tx));
}

bool p25_test_tx_active(const p25_test_tx *tx)
{
    return tx->state == P25_TX_WAITING || tx->state == P25_TX_STREAMING
        || tx->state == P25_TX_DRAINING;
}

bool p25_test_tx_start(p25_test_tx *tx, uint32_t now_us, bool armed)
{
    if (p25_test_tx_active(tx)) return false;
    p25_test_tx_init(tx);
    if (!armed) {
        tx->reason = P25_TX_NOT_ARMED;
        return false;
    }
    tx->state = P25_TX_WAITING;
    tx->started_us = now_us;
    return true;
}

void p25_test_tx_abort(p25_test_tx *tx, p25_tx_reason reason)
{
    if (p25_test_tx_active(tx)) {
        tx->reason = reason;
        tx->state = P25_TX_ABORTED;
    }
}

void p25_test_tx_poll(p25_test_tx *tx, uint32_t now_us, bool ptt_down)
{
    p25_test_tx_poll_limit(tx,now_us,ptt_down,P25_TX_MAX_US);
}

void p25_test_tx_poll_limit(p25_test_tx *tx, uint32_t now_us, bool ptt_down, uint32_t limit_us)
{
    if (!p25_test_tx_active(tx)) return;
    if (!ptt_down) {
        p25_test_tx_abort(tx, P25_TX_RELEASED);
    } else if (now_us - tx->started_us >= limit_us) {
        p25_test_tx_abort(tx, P25_TX_TIMEOUT);
    } else if (tx->state == P25_TX_DRAINING) {
        if ((int32_t)(now_us - tx->drain_until_us) >= 0) tx->state = P25_TX_DONE;
    } else if (now_us - (tx->have_boundary ? tx->last_boundary_us : tx->started_us)
               >= P25_TX_STALL_US) {
        p25_test_tx_abort(tx, P25_TX_STALLED);
    }
}

bool p25_test_tx_boundary(p25_test_tx *tx, uint32_t now_us, uint8_t block[P25_TX_BLOCK_BYTES])
{
    if (tx->state != P25_TX_WAITING && tx->state != P25_TX_STREAMING) return false;
    if (now_us - tx->started_us >= P25_TX_MAX_US) {
        p25_test_tx_abort(tx, P25_TX_TIMEOUT);
        return false;
    }
    if (tx->have_boundary) {
        uint32_t gap = now_us - tx->last_boundary_us;
        if (gap < 15000u) {
            ++tx->duplicate_irqs;
            return false;
        }
        if (gap > tx->max_gap_us) tx->max_gap_us = gap;
        if (gap > P25_TX_LATE_US) {
            ++tx->late_irqs;
            p25_test_tx_abort(tx, P25_TX_LATE);
            return false;
        }
    } else if (now_us - tx->started_us >= P25_TX_STALL_US) {
        p25_test_tx_abort(tx, P25_TX_STALLED);
        return false;
    }
    tx->last_boundary_us = now_us;
    tx->have_boundary = true;
    tx->state = P25_TX_STREAMING;
    uint32_t length = p25_test_tx_stream_bytes();
    for (uint32_t i=0; i<P25_TX_BLOCK_BYTES; ++i) {
        block[i] = stream_byte(tx->offset + i);
    }
    uint32_t left = length - tx->offset;
    tx->offset += left < P25_TX_BLOCK_BYTES ? left : P25_TX_BLOCK_BYTES;
    ++tx->blocks;
    if (tx->offset == length) {
        tx->state = P25_TX_DRAINING;
        tx->drain_until_us = now_us + P25_TX_DRAIN_US;
    }
    return true;
}
