// SPDX-License-Identifier: MIT
// Copyright (c) 2026 The RadioFoundry Authors
// P25 Phase 1 NID BCH(63,16,11) (docs/spec/p25.md). Binary BCH over GF(2^6) with the primitive polynomial
// x^6 + x + 1 (0x43) and the roots alpha^1..alpha^22. The generator polynomial is 0xCD930BDD3B2B, checked against
// every NID in a real conventional call (all zero errors) and against synthetic errors up to 11 bits.
#include <stdbool.h>
#include "p25_bch.h"

#define GF_N			63u	// the multiplicative group's order; the field uses x^6 + x + 1
#define P25_BCH_T		11	// correctable errors
#define P25_BCH_R		2 * P25_BCH_T

#define P25_BCH_GEN		0xCD930BDD3B2Bull

// alpha^i for i = 0..62, with alpha^63 = alpha^0 repeated at [63] so a sum of two logs needs no modulo
static const uint8_t gf_exp[64] = {
	0x01, 0x02, 0x04, 0x08, 0x10, 0x20, 0x03, 0x06, 0x0c, 0x18, 0x30, 0x23, 0x05, 0x0a, 0x14, 0x28,
	0x13, 0x26, 0x0f, 0x1e, 0x3c, 0x3b, 0x35, 0x29, 0x11, 0x22, 0x07, 0x0e, 0x1c, 0x38, 0x33, 0x25,
	0x09, 0x12, 0x24, 0x0b, 0x16, 0x2c, 0x1b, 0x36, 0x2f, 0x1d, 0x3a, 0x37, 0x2d, 0x19, 0x32, 0x27,
	0x0d, 0x1a, 0x34, 0x2b, 0x15, 0x2a, 0x17, 0x2e, 0x1f, 0x3e, 0x3f, 0x3d, 0x39, 0x31, 0x21, 0x01,
};
static const uint8_t gf_log[64] = {
	0x00, 0x00, 0x01, 0x06, 0x02, 0x0c, 0x07, 0x1a, 0x03, 0x20, 0x0d, 0x23, 0x08, 0x30, 0x1b, 0x12,
	0x04, 0x18, 0x21, 0x10, 0x0e, 0x34, 0x24, 0x36, 0x09, 0x2d, 0x31, 0x26, 0x1c, 0x29, 0x13, 0x38,
	0x05, 0x3e, 0x19, 0x0b, 0x22, 0x1f, 0x11, 0x2f, 0x0f, 0x17, 0x35, 0x33, 0x25, 0x2c, 0x37, 0x28,
	0x0a, 0x3d, 0x2e, 0x1e, 0x32, 0x16, 0x27, 0x2b, 0x1d, 0x3c, 0x2a, 0x15, 0x14, 0x3b, 0x39, 0x3a,
};

static uint8_t gf_mul(uint8_t a, uint8_t b)
{
	if (a == 0 || b == 0) {
		return 0;
	}
	return gf_exp[(gf_log[a] + gf_log[b]) % GF_N];
}

static uint8_t gf_inv(uint8_t a)
{
	return gf_exp[(GF_N - gf_log[a]) % GF_N];
}

// The polynomial remainder of v by the generator (v < 2^63, so the degree is at most 62)
static uint64_t bch_remainder(uint64_t v)
{
	while (v >= (1ull << 47)) {
		int d = 0;
		for (uint64_t t = v >> 47; t != 0; t >>= 1) {
			d++;
		}
		v ^= P25_BCH_GEN << (d - 1);
	}
	return v;
}

uint64_t p25_bch_encode(uint16_t info)
{
	uint64_t c = (uint64_t)info << 47;
	return c | bch_remainder(c);
}

// Syndromes S[0..21], S[j-1] = r(alpha^j)
static void syndromes(uint64_t cw, uint8_t *s)
{
	for (int j = 1; j <= P25_BCH_R; j++) {
		uint8_t v = 0;
		for (int b = 0; b < P25_BCH_CODE_BITS; b++) {
			if ((cw >> b) & 1u) {
				v ^= gf_exp[(j * b) % GF_N];
			}
		}
		s[j - 1] = v;
	}
}

// Berlekamp-Massey: the error locator sigma(x) = C[0] + C[1] x + ... and its degree L
static int error_locator(const uint8_t *s, uint8_t *c)
{
	uint8_t b[40] = { 0 }, t[40];
	int l = 0, m = 1;
	uint8_t last = 1;

	for (int i = 0; i < 40; i++) {
		c[i] = 0;
	}
	c[0] = 1;
	b[0] = 1;

	for (int n = 0; n < P25_BCH_R; n++) {
		uint8_t d = s[n];
		for (int i = 1; i <= l && i <= n; i++) {
			d ^= gf_mul(c[i], s[n - i]);
		}
		if (d == 0) {
			m++;
			continue;
		}
		uint8_t coef = gf_mul(d, gf_inv(last));
		if (2 * l <= n) {
			for (int i = 0; i < 40; i++) {
				t[i] = c[i];
			}
			for (int i = 0; i + m < 40; i++) {
				c[i + m] ^= gf_mul(coef, b[i]);
			}
			l = n + 1 - l;
			for (int i = 0; i < 40; i++) {
				b[i] = t[i];
			}
			last = d;
			m = 1;
		} else {
			for (int i = 0; i + m < 40; i++) {
				c[i + m] ^= gf_mul(coef, b[i]);
			}
			m++;
		}
	}
	return l;
}

// Chien search: the positions p (bit index) whose alpha^-p is a root of sigma
static int error_positions(const uint8_t *c, int l, uint8_t *pos)
{
	int n = 0;

	for (int p = 0; p < P25_BCH_CODE_BITS; p++) {
		uint8_t a = gf_exp[(GF_N - p) % GF_N];
		uint8_t v = 0;
		for (int i = l; i >= 0; i--) {
			v = gf_mul(v, a) ^ c[i];
		}
		if (v == 0) {
			if (n >= l) {
				return -1;
			}
			pos[n++] = (uint8_t)p;
		}
	}
	return n == l ? n : -1;
}

int p25_bch_decode(uint64_t cw, uint16_t *info)
{
	uint8_t s[P25_BCH_R];
	uint8_t loc[40], pos[P25_BCH_T];

	syndromes(cw, s);
	bool clean = true;
	for (int i = 0; i < P25_BCH_R; i++) {
		if (s[i] != 0) {
			clean = false;
		}
	}
	if (clean) {
		*info = (uint16_t)(cw >> 47);
		return 0;
	}

	int l = error_locator(s, loc);
	if (l <= 0 || l > P25_BCH_T) {
		return -1;
	}
	if (error_positions(loc, l, pos) < 0) {
		return -1;
	}

	uint64_t corr = cw;
	for (int i = 0; i < l; i++) {
		corr ^= 1ull << pos[i];
	}
	syndromes(corr, s);
	for (int i = 0; i < P25_BCH_R; i++) {
		if (s[i] != 0) {
			return -1;
		}
	}
	*info = (uint16_t)(corr >> 47);
	return l;
}
