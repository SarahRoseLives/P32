// SPDX-License-Identifier: MIT
// Copyright (c) 2026 The RadioFoundry Authors
// P25 Phase 1 physical layer receive (core/p25): the NID BCH code, the frame sync, status-symbol removal and the
// NAC/DUID. The two real vectors are one LDU1 and one LDU2 sliced from a conventional call, NAC 0x293.
#include <stdio.h>
#include <string.h>
#include "p25/p25_bch.h"
#include "p25/p25_phy.h"

static int failures;

#define CHECK(cond) do { if (!(cond)) { printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); failures++; } } while (0)

// One LDU2 (DUID 0xA) and one LDU1 (DUID 0x5), 60 sliced symbols each starting at the frame sync. The status symbol
// at relative 35 is in the vectors and must be skipped by the receiver.
static const int8_t cap_ldu2[60] = {
	 3,  3,  3,  3,  3, -3,  3,  3, -3, -3,  3,  3, -3, -3, -3, -3,
	 3, -3,  3, -3, -3, -3, -3, -3,  1, -1, -1,  3,  1, -3, -1, -1,
	-1, -3, -1,  1, -1, -1,  3,  1, -3, -1, -3, -3, -1, -3,  1,  1,
	-1,  3, -1, -1, -1,  1, -1, -1, -3,  3, -3,  3,
};
static const int8_t cap_ldu1[60] = {
	 3,  3,  3,  3,  3, -3,  3,  3, -3, -3,  3,  3, -3, -3, -3, -3,
	 3, -3,  3, -3, -3, -3, -3, -3,  1, -1, -1,  3,  1, -3,  3,  3,
	 3,  3,  3,  1,  3, -3, -1, -3, -3,  1, -1, -3,  1,  3, -1,  3,
	 3,  1, -3,  3,  1,  1, -3,  3, -3,  3, -1, -3,
};

static p25_nid_t got;
static int got_count;

static void on_nid(void *ctx, const p25_nid_t *nid)
{
	(void)ctx;
	got = *nid;
	got_count++;
}

static void feed(p25_phy_t *p, const int8_t *sym, int n)
{
	for (int i = 0; i < n; i++) {
		p25_phy_push_symbol(p, sym[i]);
	}
}

static void test_bch(void)
{
	static const uint16_t infos[] = { 0x2935, 0x293A, 0x84B0, 0x1234, 0xFFFF };

	// Flip n distinct bits spread through the codeword
	for (unsigned k = 0; k < sizeof infos / sizeof infos[0]; k++) {
		uint64_t cw = p25_bch_encode(infos[k]);
		uint16_t info = 0;
		CHECK(p25_bch_decode(cw, &info) == 0 && info == infos[k]);
		for (int n = 1; n <= 11; n++) {
			uint64_t bad = cw;
			bool used[P25_BCH_CODE_BITS] = { false };
			uint32_t lcg = 0x12345678u + (uint32_t)n * 2654435761u + k;
			int flipped = 0;
			while (flipped < n) {
				lcg = lcg * 1664525u + 1013904223u;
				int b = (int)((lcg >> 16) % P25_BCH_CODE_BITS);
				if (used[b]) {
					continue;
				}
				used[b] = true;
				bad ^= 1ull << b;
				flipped++;
			}
			CHECK(p25_bch_decode(bad, &info) == n && info == infos[k]);
		}
		// Twelve errors is past the radius: it must not report the original information as corrected
		uint64_t bad = cw;
		for (int b = 0; b < 12; b++) {
			bad ^= 1ull << (b * 5);
		}
		info = 0;
		int r = p25_bch_decode(bad, &info);
		CHECK(r < 0 || info != infos[k]);
	}
}

static void test_frame(const int8_t *sym, uint8_t duid)
{
	p25_phy_t p;

	memset(&got, 0, sizeof got);
	got_count = 0;
	p25_phy_init(&p, on_nid, NULL);
	feed(&p, sym, 60);
	CHECK(got_count == 1);
	CHECK(got.nac == 0x293 && got.duid == duid && got.errors == 0 && got.flag_ok);
}

static void test_leading(void)
{
	static const int8_t lead[] = { 1, -1, 1, -1, 3, -3, 1, 1, -1, -3 };
	p25_phy_t p;

	memset(&got, 0, sizeof got);
	got_count = 0;
	p25_phy_init(&p, on_nid, NULL);
	feed(&p, lead, (int)(sizeof lead / sizeof lead[0]));
	feed(&p, cap_ldu1, 60);
	CHECK(got_count == 1 && got.nac == 0x293 && got.duid == P25_DUID_LDU1);
}

static void test_polarity(void)
{
	int8_t inv[60];
	p25_phy_t p;

	for (int i = 0; i < 60; i++) {
		inv[i] = (int8_t)-cap_ldu2[i];
	}
	memset(&got, 0, sizeof got);
	got_count = 0;
	p25_phy_init(&p, on_nid, NULL);
	feed(&p, inv, 60);
	CHECK(got_count == 1 && got.nac == 0x293 && got.duid == P25_DUID_LDU2 && got.errors == 0);
}

int main(void)
{
	test_bch();
	test_frame(cap_ldu2, P25_DUID_LDU2);
	test_frame(cap_ldu1, P25_DUID_LDU1);
	test_leading();
	test_polarity();

	if (failures) {
		printf("p25PhyTest: %d failures\n", failures);
		return 1;
	}
	printf("p25PhyTest: all passed\n");
	return 0;
}
