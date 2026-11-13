// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 The RadioFoundry Authors
// P25 Phase 1 trunked control-channel transmit (core/p25/p25_cc_tx): the known TSBKs must encode to the captured
// bytes, every block must survive p25_tsbk_decode from the built frame, and the frame's FSW, NID, status symbols,
// padding and 45-byte packing must land exactly.
#include <stdio.h>
#include <string.h>
#include "p25/p25_bch.h"
#include "p25/p25_cc_tx.h"
#include "p25/p25_tsbk.h"
#include "p25/p25_rx.h"

static int failures;

#define CHECK(cond) do { if (!(cond)) { printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); failures++; } } while (0)

static const uint8_t fsw[24] = {
	1, 1, 1, 1, 1, 3, 1, 1, 3, 3, 1, 1, 3, 3, 3, 3, 1, 3, 1, 3, 3, 3, 3, 3,
};

// The on-air status symbols sit at dibits 35, 71, 107, 143 and 179, counted from the FSW's first
static int is_status(int i)
{
	return i >= 35 && ((i - 35) % 36) == 0;
}

// The frame without the status symbols: 24 FSW + 32 NID dibits, the 98 TSBK dibits and the zero padding
static int collect_data(const uint8_t frame[P25_CC_FRAME_DIBITS], uint8_t data[175])
{
	int n = 0;

	for (int i = 0; i < P25_CC_FRAME_DIBITS; i++) {
		if (is_status(i)) {
			continue;
		}
		data[n++] = frame[i];
	}
	return n;
}

static void extract_tsbk_bits(const uint8_t frame[P25_CC_FRAME_DIBITS], uint8_t bits[P25_TSBK_BITS])
{
	uint8_t data[175];

	CHECK(collect_data(frame, data) == 175);
	for (int i = 0; i < P25_TSBK_DIBITS; i++) {
		uint8_t d = data[56 + i];

		bits[2 * i] = (uint8_t)((d >> 1) & 1u);
		bits[2 * i + 1] = (uint8_t)(d & 1u);
	}
}

static void round_trip(const char *name, const uint8_t tsbk[P25_CC_TSBK_BYTES], uint16_t nac)
{
	uint8_t frame[P25_CC_FRAME_DIBITS];
	uint8_t bits[P25_TSBK_BITS];
	uint8_t out[P25_TSBK_BYTES];
	int metric;

	p25_cc_build_frame_dibits(frame, nac, tsbk);
	extract_tsbk_bits(frame, bits);
	metric = p25_tsbk_decode(bits, NULL, out);
	CHECK(metric >= 0);
	if (metric < 0 || memcmp(out, tsbk, P25_CC_TSBK_BYTES) != 0) {
		printf("FAIL %s: metric %d, decoded", name, metric);
		for (int i = 0; i < P25_CC_TSBK_BYTES; i++) {
			printf(" %02X", out[i]);
		}
		printf("\n");
		failures++;
	}
}

static void expect_bytes(const char *name, const uint8_t got[P25_CC_TSBK_BYTES],
			 const uint8_t want[P25_CC_TSBK_BYTES])
{
	if (memcmp(got, want, P25_CC_TSBK_BYTES) != 0) {
		printf("FAIL %s: got", name);
		for (int i = 0; i < P25_CC_TSBK_BYTES; i++) {
			printf(" %02X", got[i]);
		}
		printf("\n");
		failures++;
	}
}

// The control-channel frame must also survive the continuous software receiver's framer, including DUID 7.
static void rx_round_trip(void)
{
	uint8_t tsbk[P25_CC_TSBK_BYTES] = { 0 };
	uint8_t frame[P25_CC_FRAME_DIBITS];
	p25_rx rx;
	bool complete = false;

	p25_tsbk_build(tsbk, 0x3d, 0);
	p25_cc_build_frame_dibits(frame, 0x123u, tsbk);
	p25_rx_init(&rx);
	for (int i = 0; i < P25_CC_FRAME_DIBITS; i++) {
		complete = p25_rx_push(&rx, frame[i]) || complete;
	}
	CHECK(complete);
	CHECK(rx.nac == 0x123u);
	CHECK(rx.duid == 7u);
	CHECK(rx.bad_nid == 0u);
}

int main(void)
{
	rx_round_trip();
	static const uint8_t net_status_want[P25_CC_TSBK_BYTES] = {
		0xBB, 0x00, 0x00, 0xBE, 0xEF, 0x00, 0x01, 0x00, 0x00, 0x00, 0xB5, 0xA7,
	};
	static const uint8_t iden_up_want[P25_CC_TSBK_BYTES] = {
		0xBD, 0x00, 0x03, 0x20, 0x00, 0x64, 0x05, 0x2B, 0x2A, 0x78, 0xD4, 0xF2,
	};
	static const uint8_t rfss_want[P25_CC_TSBK_BYTES] = {
		0xBA, 0x00, 0x00, 0x00, 0x01, 0x01, 0x01, 0x00, 0x00, 0x00, 0xBB, 0xD4,
	};
	static const uint8_t grant_want[P25_CC_TSBK_BYTES] = {
		0x80, 0x00, 0x02, 0x00, 0x01, 0x00, 0x01, 0x00, 0x00, 0x00, 0xA7, 0x79,
	};
	uint8_t tsbk[P25_CC_TSBK_BYTES];
	uint8_t guarded[P25_CC_FRAME_DIBITS + 2];
	const uint8_t *frame = &guarded[1];
	uint8_t data[175];
	uint16_t info = 0;
	uint64_t nid = 0;
	unsigned statuses = 0;

	CHECK(P25_CC_FRAME_DIBITS == 180);
	CHECK(P25_CC_FRAME_BYTES == 45);

	// Network Status Broadcast (captured frame)
	p25_tsbk_build(tsbk, 0x3B,
		       ((uint64_t)(0xBEEF0u & 0xFFFFFu) << 36) | ((uint64_t)(0x001u & 0xFFFu) << 24));
	expect_bytes("NetStatus", tsbk, net_status_want);
	round_trip("NetStatus", tsbk, 0x293);

	// Identifier Update (captured frame, 433.575 MHz base)
	p25_tsbk_build(tsbk, 0x3D,
		       ((uint64_t)100u << 51) | ((uint64_t)100u << 32) | ((433575000u / 5u) & 0xFFFFFFFFu));
	expect_bytes("IdenUp", tsbk, iden_up_want);
	round_trip("IdenUp", tsbk, 0x293);

	// RFSS Status Broadcast (captured frame)
	p25_tsbk_build(tsbk, 0x3A,
		       ((uint64_t)(0x001u & 0xFFFu) << 40) | ((uint64_t)1u << 32) | ((uint64_t)1u << 24));
	expect_bytes("RFSSStatus", tsbk, rfss_want);
	round_trip("RFSSStatus", tsbk, 0x293);

	// Group Voice Grant (captured frame): channel id 0, channel 1, talkgroup 1
	p25_tsbk_build(tsbk, 0x00,
		       ((uint64_t)0x02u << 56) | ((uint64_t)((0u << 12) | 1u) << 40) | ((uint64_t)1u << 24));
	expect_bytes("GroupVoiceGrant", tsbk, grant_want);
	round_trip("GroupVoiceGrant", tsbk, 0x293);

	// Unit Registration Request: emergency, capability, WACN, system ID and source ID in their bytes
	p25_tsbk_unit_registration(tsbk, 0xBEEF0u, 0x123u, 0xABCDEFu, 1u, 0x55u);
	CHECK(tsbk[0] == 0xAC);
	CHECK(tsbk[1] == 0x00);
	CHECK(tsbk[2] == 0xD5);		// emergency bit 7 + capability 0x55
	CHECK(tsbk[3] == 0xBE);		// WACN 19..12
	CHECK(tsbk[4] == 0xEF);		// WACN 11..4
	CHECK(tsbk[5] == 0x01);		// WACN 3..0 (high nibble) and SysID 11..8 (low nibble)
	CHECK(tsbk[6] == 0x23);		// SysID 7..0
	CHECK(tsbk[7] == 0xAB);
	CHECK(tsbk[8] == 0xCD);
	CHECK(tsbk[9] == 0xEF);
	round_trip("UnitRegistration", tsbk, 0x123);

	// Group Affiliation Response: reserved byte, system ID, group and source ID
	p25_tsbk_group_affiliation(tsbk, 0x123u, 0x4567u, 0xABCDEFu);
	CHECK(tsbk[0] == 0xA8);
	CHECK(tsbk[1] == 0x00);
	CHECK(tsbk[2] == 0x00);		// reserved
	CHECK(tsbk[3] == 0x01);		// SysID 11..8
	CHECK(tsbk[4] == 0x23);		// SysID 7..0
	CHECK(tsbk[5] == 0x45);		// group high byte
	CHECK(tsbk[6] == 0x67);		// group low byte
	CHECK(tsbk[7] == 0xAB);
	CHECK(tsbk[8] == 0xCD);
	CHECK(tsbk[9] == 0xEF);
	round_trip("GroupAffiliation", tsbk, 0x123);

	// Group Voice Channel Request (ISP 0x00): options, group 256 and source 2
	p25_tsbk_group_voice_request(tsbk, 256u, 2u, 0u);
	CHECK(tsbk[0] == 0x80);
	CHECK(tsbk[1] == 0x00);
	CHECK(tsbk[2] == 0x00);		// options
	CHECK(tsbk[3] == 0x00);
	CHECK(tsbk[4] == 0x00);
	CHECK(tsbk[5] == 0x01);		// group high byte
	CHECK(tsbk[6] == 0x00);		// group low byte
	CHECK(tsbk[7] == 0x00);
	CHECK(tsbk[8] == 0x00);
	CHECK(tsbk[9] == 0x02);		// source
	round_trip("GroupVoiceRequest", tsbk, 0x123);

	// Unit De-Registration Request (ISP 0x2B): source in bytes 7..9
	p25_tsbk_unit_deregistration(tsbk, 0xABCDEFu);
	CHECK(tsbk[0] == 0xAB);
	CHECK(tsbk[1] == 0x00);
	CHECK(tsbk[7] == 0xAB);
	CHECK(tsbk[8] == 0xCD);
	CHECK(tsbk[9] == 0xEF);
	round_trip("UnitDeregistration", tsbk, 0x123);

	// Frame layout: the FSW, a decodable NID, the five status symbols, the zero padding and no off-by-one
	memset(guarded, 0xAA, sizeof guarded);
	p25_cc_build_frame_dibits(&guarded[1], 0x293u, net_status_want);
	CHECK(guarded[0] == 0xAA);
	CHECK(guarded[P25_CC_FRAME_DIBITS + 1] == 0xAA);

	CHECK(collect_data(frame, data) == 175);
	for (int i = 0; i < 24; i++) {
		CHECK(frame[i] == fsw[i]);
	}
	for (int i = 0; i < 32; i++) {
		nid = (nid << 2) | data[24 + i];
	}
	CHECK(p25_bch_decode(nid >> 1, &info) == 0);
	CHECK(info == ((0x293u << 4) | 0x7u));
	CHECK((nid & 1u) == 0u);	// a TSDU's NID flag bit is 0

	for (int i = 0; i < P25_CC_FRAME_DIBITS; i++) {
		if (is_status(i)) {
			CHECK(frame[i] == (statuses == 0 ? 0x03 : 0x02));
			statuses++;
		}
	}
	CHECK(statuses == 5);
	for (int i = 158; i <= 178; i++) {
		CHECK(frame[i] == 0x00);
	}

	// The 45-byte frame is the same dibits four to a byte, most significant dibit first, with no overflow
	{
		uint8_t packed_guard[P25_CC_FRAME_BYTES + 2];
		const uint8_t *packed = &packed_guard[1];

		memset(packed_guard, 0xAA, sizeof packed_guard);
		p25_cc_build_frame(&packed_guard[1], 0x293u, net_status_want);
		CHECK(packed_guard[0] == 0xAA);
		CHECK(packed_guard[P25_CC_FRAME_BYTES + 1] == 0xAA);
		for (int i = 0; i < P25_CC_FRAME_BYTES; i++) {
			uint8_t want = (uint8_t)((frame[4 * i] << 6) | (frame[4 * i + 1] << 4) |
						 (frame[4 * i + 2] << 2) | frame[4 * i + 3]);

			CHECK(packed[i] == want);
		}
	}

	if (failures == 0) {
		printf("p25CcTxTest: all checks passed\n");
	}
	return failures == 0 ? 0 : 1;
}
