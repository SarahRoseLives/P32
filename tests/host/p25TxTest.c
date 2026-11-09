// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (c) 2026 DM32 P25 research contributors
// Copyright (c) 2026 The RadioFoundry Authors
// Conventional P25 transmit framing and timing (core/p25/tx). Ported from DM32-Multimode tests/test_p25_tx.c. The
// stream is rebuilt from p25_test_vectors.h: a 240-byte 0x77 preamble, the HDU, 28 alternating LDU1/LDU2 pairs, the
// TDU, then an explicit 0x77 tail. No fixture file is read at runtime.
#include <stdio.h>
#include "p25/tx/p25_test_tx.h"
#include "p25/tx/p25_test_vectors.h"

static int failures;

#define CHECK(cond) do { if (!(cond)) { printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); failures++; } } while (0)

static uint8_t expected_byte(uint32_t index)
{
	if (index < P25_TEST_PREAMBLE_BYTES) {
		return 0x77;
	}
	index -= P25_TEST_PREAMBLE_BYTES;
	if (index < sizeof p25_test_hdu) {
		return p25_test_hdu[index];
	}
	index -= sizeof p25_test_hdu;
	const uint32_t pair_bytes = sizeof p25_test_ldu1 + sizeof p25_test_ldu2;
	if (index < P25_TEST_PAIR_REPEATS * pair_bytes) {
		index %= pair_bytes;
		if (index < sizeof p25_test_ldu1) {
			return p25_test_ldu1[index];
		}
		return p25_test_ldu2[index - sizeof p25_test_ldu1];
	}
	index -= P25_TEST_PAIR_REPEATS * pair_bytes;
	if (index < sizeof p25_test_tdu) {
		return p25_test_tdu[index];
	}
	return 0x77;
}

static void test_stream_vectors(void)
{
	const uint32_t tdu = P25_TEST_PREAMBLE_BYTES + sizeof p25_test_hdu
		+ P25_TEST_PAIR_REPEATS * (sizeof p25_test_ldu1 + sizeof p25_test_ldu2);
	bool preamble = true;

	CHECK(p25_test_tx_stream_bytes() == 12453u);
	CHECK(tdu + sizeof p25_test_tdu == p25_test_tx_stream_bytes());
	CHECK(p25_test_hdu[0] == 0x55 && p25_test_ldu1[0] == 0x55);
	CHECK(p25_test_ldu2[0] == 0x55 && p25_test_tdu[0] == 0x55);
	for (uint32_t i = 0; i < P25_TEST_PREAMBLE_BYTES; i++) {
		preamble = preamble && expected_byte(i) == 0x77;
	}
	CHECK(preamble);
	CHECK(expected_byte(P25_TEST_PREAMBLE_BYTES) == p25_test_hdu[0]);
	CHECK(expected_byte(P25_TEST_PREAMBLE_BYTES + sizeof p25_test_hdu) == p25_test_ldu1[0]);
	CHECK(expected_byte(P25_TEST_PREAMBLE_BYTES + sizeof p25_test_hdu + sizeof p25_test_ldu1)
	      == p25_test_ldu2[0]);
	CHECK(expected_byte(tdu) == p25_test_tdu[0]);
	CHECK(expected_byte(p25_test_tx_stream_bytes()) == 0x77);
}

static void test_stream(void)
{
	p25_test_tx tx;
	uint8_t block[P25_TX_BLOCK_BYTES];
	uint32_t now = 1000;
	uint32_t bytes = 0;

	p25_test_tx_init(&tx);
	CHECK(!p25_test_tx_start(&tx, 0, false));
	CHECK(!p25_test_tx_active(&tx) && tx.reason == P25_TX_NOT_ARMED);
	CHECK(p25_test_tx_start(&tx, 0, true));
	CHECK(!p25_test_tx_start(&tx, 0, true));

	while (p25_test_tx_boundary(&tx, now, block)) {
		for (unsigned i = 0; i < P25_TX_BLOCK_BYTES; i++) {
			CHECK(block[i] == expected_byte(bytes));
			bytes++;
		}
		// The same-boundary SYS + TS IRQ must not consume another block.
		CHECK(!p25_test_tx_boundary(&tx, now + 100, block));
		now += 30000;
	}

	CHECK(tx.state == P25_TX_DRAINING);
	CHECK(tx.blocks == 346);
	CHECK(bytes == 12456);
	CHECK(tx.offset == 12453);
	CHECK(tx.max_gap_us == 30000);
	CHECK(tx.duplicate_irqs == 345);

	p25_test_tx_poll(&tx, tx.drain_until_us - 1, true);
	CHECK(tx.state == P25_TX_DRAINING);
	p25_test_tx_poll(&tx, tx.drain_until_us, true);
	CHECK(tx.state == P25_TX_DONE);
	CHECK(!p25_test_tx_active(&tx));
}

static void test_aborts(void)
{
	p25_test_tx tx;
	uint8_t block[P25_TX_BLOCK_BYTES];

	CHECK(p25_test_tx_start(&tx, 0, true));
	p25_test_tx_poll(&tx, 100, false);
	CHECK(tx.state == P25_TX_ABORTED && tx.reason == P25_TX_RELEASED);
	CHECK(!p25_test_tx_boundary(&tx, 1000, block));

	CHECK(p25_test_tx_start(&tx, 0, true));
	p25_test_tx_poll(&tx, P25_TX_STALL_US, true);
	CHECK(tx.reason == P25_TX_STALLED);

	CHECK(p25_test_tx_start(&tx, 0, true));
	CHECK(!p25_test_tx_boundary(&tx, P25_TX_STALL_US, block));
	CHECK(tx.reason == P25_TX_STALLED);

	CHECK(p25_test_tx_start(&tx, 0, true));
	CHECK(p25_test_tx_boundary(&tx, 1000, block));
	CHECK(!p25_test_tx_boundary(&tx, 1000 + P25_TX_LATE_US + 1, block));
	CHECK(tx.reason == P25_TX_LATE && tx.late_irqs == 1);

	CHECK(p25_test_tx_start(&tx, 0, true));
	p25_test_tx_poll(&tx, P25_TX_MAX_US, true);
	CHECK(tx.reason == P25_TX_TIMEOUT);
}

static void test_wrap(void)
{
	p25_test_tx tx;
	uint8_t block[P25_TX_BLOCK_BYTES];
	const uint32_t base = 0xfffffff0u;

	p25_test_tx_init(&tx);
	CHECK(p25_test_tx_start(&tx, base, true));
	CHECK(p25_test_tx_boundary(&tx, base + 1000u, block));
	CHECK(p25_test_tx_boundary(&tx, base + 31000u, block));
	CHECK(tx.state == P25_TX_STREAMING);
	p25_test_tx_abort(&tx, P25_TX_CANCELLED);
	CHECK(tx.state == P25_TX_ABORTED && tx.reason == P25_TX_CANCELLED);
}

int main(void)
{
	test_stream_vectors();
	test_stream();
	test_aborts();
	test_wrap();

	if (failures) {
		printf("p25TxTest: %d failures\n", failures);
		return 1;
	}
	printf("p25TxTest: all passed\n");
	return 0;
}
