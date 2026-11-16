// SPDX-License-Identifier: MIT
// Copyright (c) 2026 The RadioFoundry Authors
// Motorola ADP keystream: RC4 key || MI[0:8], 256 bytes dropped, LDU1 offset 11, LDU2 offset 112 (docs/spec/p25.md 9.1)
#include <stdio.h>
#include <string.h>
#include "p25/p25_adp.h"

static int fails;
#define CHECK(c) do { if (!(c)) { printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); fails++; } } while (0)

static void dump(char *out, const uint8_t *p, unsigned n)
{
	static const char h[] = "0123456789abcdef";
	for (unsigned i = 0; i < n; i++) {
		out[2 * i] = h[p[i] >> 4];
		out[2 * i + 1] = h[p[i] & 15];
	}
	out[2 * n] = 0;
}

int main(void)
{
	uint8_t out[P25_ADP_STREAM_LEN], again[P25_ADP_STREAM_LEN];
	char text[2 * P25_ADP_STREAM_LEN + 1];
	const uint8_t key[P25_ADP_KEY_LEN] = { 0x00, 0x00, 0x09, 0x71, 0x30 };
	const uint8_t mi[9] = { 0x16, 0x31, 0x93, 0x13, 0xa6, 0xb1, 0x9c, 0xc5, 0x00 };

	// Published RC4 keystream for key "Key" (not ADP, but the same primitive)
	p25_adp_rc4((const uint8_t *)"Key", 3, out, 9, 0);
	dump(text, out, 9);
	CHECK(strcmp(text, "eb9f7781b734ca72a7") == 0);

	// ADP: key 0000097130, MI 16319313a6b19cc500, after the 256-byte drop
	p25_adp_keystream(key, mi, out);
	dump(text, out, 16);
	CHECK(strcmp(text, "d940e2b02743c72c16d451c1c5634fba") == 0);
	p25_adp_keystream(key, mi, again);
	CHECK(memcmp(out, again, sizeof out) == 0);
	CHECK(out[P25_ADP_LDU1_OFFSET] == 0xc1u);	// keystream at LDU1 frame 0
	CHECK(out[P25_ADP_LDU2_OFFSET] == 0xb0u);	// keystream at LDU2 frame 0

	// The same operation decrypts and encrypts one 11-byte IMBE payload at each LDU offset
	for (unsigned frame = 0; frame < 9; frame++) {
		uint8_t p[11] = { 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11 };
		p25_adp_apply(p, out, P25_ADP_LDU1_OFFSET, frame);
		CHECK(memcmp(p, (const uint8_t[]){ 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11 }, 11) != 0);
		p25_adp_apply(p, out, P25_ADP_LDU1_OFFSET, frame);
		CHECK(memcmp(p, (const uint8_t[]){ 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11 }, 11) == 0);
	}
	for (unsigned frame = 0; frame < 9; frame++) {
		uint8_t p[11] = { 0 };
		p25_adp_apply(p, out, P25_ADP_LDU2_OFFSET, frame);
		p25_adp_apply(p, out, P25_ADP_LDU2_OFFSET, frame);
		CHECK(p[10] == 0);
	}

	if (fails) {
		printf("# FAILED p25AdpTest: %d\n", fails);
		return 1;
	}
	printf("# PASSED p25AdpTest\n");
	return 0;
}
