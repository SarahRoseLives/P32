// SPDX-License-Identifier: ISC
// Copyright (c) 2026 The RadioFoundry Authors
// P25 Phase 1 TSBK (core/p25/p25_tsbk): four control-channel blocks captured at NAC 0x293, 196 bits each after the
// NID with the status symbols removed. They must deinterleave, Viterbi-decode and pass the CRC-16 to these bytes.
#include <stdio.h>
#include <string.h>
#include "p25/p25_tsbk.h"

static int failures;

#define CHECK(cond) do { if (!(cond)) { printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); failures++; } } while (0)

typedef struct {
	const char *bits;		// 49 hex digits = 196 bits, first transmitted bit is the top bit
	uint8_t out[P25_TSBK_BYTES];
} vector_t;

static const vector_t vectors[] = {
	{ "1521452E221D542246222224A6228422222B7422682C22203",
	  { 0xBB, 0x00, 0x00, 0xBE, 0xEF, 0x00, 0x01, 0x00, 0x00, 0x00, 0xB5, 0xA7 } },
	{ "1E252C2E57FF942A12D2113B8822927CAA605B2F22E04A8E1",
	  { 0xBD, 0x00, 0x03, 0x20, 0x00, 0x64, 0x05, 0x2B, 0x2A, 0x78, 0xD4, 0xF2 } },
	{ "12292E2E221BE9222222222A32222222222362212C2C22237",
	  { 0x80, 0x00, 0x02, 0x00, 0x01, 0x00, 0x01, 0x00, 0x00, 0x00, 0xA7, 0x79 } },
	{ "19222EEE2218242222222224B622222222260A222CCC2224E",
	  { 0xBA, 0x00, 0x00, 0x00, 0x01, 0x01, 0x01, 0x00, 0x00, 0x00, 0xBB, 0xD4 } },
};

static int hexval(char c)
{
	if (c >= '0' && c <= '9') {
		return c - '0';
	}
	if (c >= 'A' && c <= 'F') {
		return c - 'A' + 10;
	}
	return c - 'a' + 10;
}

static void hex_to_bits(const char *h, uint8_t *bits)
{
	for (int i = 0; i < P25_TSBK_BITS; i++) {
		bits[i] = (uint8_t)((hexval(h[i / 4]) >> (3 - (i % 4))) & 1);
	}
}

int main(void)
{
	uint8_t bits[P25_TSBK_BITS];
	uint8_t out[P25_TSBK_BYTES];

	for (unsigned v = 0; v < sizeof vectors / sizeof vectors[0]; v++) {
		int metric;

		hex_to_bits(vectors[v].bits, bits);
		metric = p25_tsbk_decode(bits, NULL, out);
		CHECK(metric >= 0);
		CHECK(memcmp(out, vectors[v].out, P25_TSBK_BYTES) == 0);
		if (memcmp(out, vectors[v].out, P25_TSBK_BYTES) != 0) {
			printf("vector %u got", v);
			for (int i = 0; i < P25_TSBK_BYTES; i++) {
				printf(" %02X", out[i]);
			}
			printf(" metric %d\n", metric);
		}
	}

	// A block of zeros must not pass the CRC
	memset(bits, 0, sizeof bits);
	CHECK(p25_tsbk_decode(bits, NULL, out) < 0);

	// Erasure decoding: the chip's DMR deframer replaces the A (CACH/slot type) and F (embedded signalling) fields of
	// a slot; those on-air dibits of the TSBK must be erased and still decode. Vector 3 (the RFSS status) with its
	// A/F dibits (0, 1 and 45..67, the same positions the bench sees) zeroed and marked erased
	{
		uint8_t er[P25_TSBK_DIBITS] = { 0 };
		static const uint8_t af[25] = { 0, 1, 45, 46, 47, 48, 49, 50, 51, 52, 53, 54, 55, 56, 57, 58,
						59, 60, 61, 62, 63, 64, 65, 66, 67 };
		uint8_t bits[P25_TSBK_BITS];
		int metric;

		hex_to_bits(vectors[3].bits, bits);
		for (unsigned i = 0; i < 25; i++) {
			er[af[i]] = 1;
			bits[2 * af[i]] = 0;
			bits[2 * af[i] + 1] = 0;
		}
		metric = p25_tsbk_decode(bits, er, out);
		CHECK(metric >= 0);
		CHECK(memcmp(out, vectors[3].out, P25_TSBK_BYTES) == 0);
		if (memcmp(out, vectors[3].out, P25_TSBK_BYTES) != 0) {
			printf("erasure vector got");
			for (int i = 0; i < P25_TSBK_BYTES; i++) {
				printf(" %02X", out[i]);
			}
			printf(" metric %d\n", metric);
		}
	}

	if (failures == 0) {
		printf("p25TsbkTest: all checks passed\n");
	}
	return failures == 0 ? 0 : 1;
}
