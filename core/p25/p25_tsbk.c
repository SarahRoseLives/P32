// SPDX-License-Identifier: ISC
// Copyright (c) 2026 The RadioFoundry Authors
// P25 Phase 1 TSBK decode: deinterleave 98 dibits, 4-state Viterbi over 49 nibbles, pack 12 bytes, check the CRC.
// The transition set (dtm), the metric bias and the CRC-16 are dsd-neo's (ISC, Copyright (C) 2026 arancormonk
// <180709949+arancormonk@users.noreply.github.com>).
#include <string.h>
#include "p25_tsbk.h"

// The standard's 98-dibit interleaver: four offset groups of eight, 13 + 12 + 12 + 12 pairs
static uint8_t interleave98(uint8_t i)
{
	uint8_t g = (uint8_t)(i >> 1);
	uint8_t off = 0;

	if (g >= 13u) {
		g -= 13u;
		off = 2;
	}
	if (g >= 12u) {
		g -= 12u;
		off = 4;
	}
	if (g >= 12u) {
		g -= 12u;
		off = 6;
	}
	return (uint8_t)(off + 8u * g + (i & 1u));
}

// Dibit-pair to trellis-dibit transition matrix: (previous state << 2) | next state -> expected nibble
static const uint8_t dtm[16] = { 2, 12, 1, 15, 14, 0, 13, 3, 9, 7, 10, 4, 5, 11, 6, 8 };

static uint8_t hamming4(uint8_t a, uint8_t b)
{
	uint8_t v = (uint8_t)((a ^ b) & 0xFu);
	uint8_t n = 0;

	while (v != 0) {
		n = (uint8_t)(n + (v & 1u));
		v >>= 1;
	}
	return n;
}

// CRC-16 CCITT over one bit per element: init 0, polynomial 0x1021, final complement (dsd-neo p25_crc.c)
static uint16_t crc16_ccitt(const uint8_t *bits, int n)
{
	uint16_t crc = 0;

	for (int i = 0; i < n; i++) {
		if (((crc >> 15) & 1u) ^ (bits[i] & 1u)) {
			crc = (uint16_t)((crc << 1) ^ 0x1021u);
		} else {
			crc = (uint16_t)(crc << 1);
		}
	}
	return (uint16_t)(crc ^ 0xFFFFu);
}

int p25_tsbk_decode(const uint8_t bits[P25_TSBK_BITS], const uint8_t erasures[P25_TSBK_DIBITS],
		    uint8_t out[P25_TSBK_BYTES])
{
	uint8_t dei[196];
	uint8_t era[196];
	uint8_t states[49];
	uint8_t back[49][4];
	uint32_t prev[4] = { 0, 256, 256, 256 }, cur[4];

	// Undo the interleave: dibit i arrives at position interleave98(i), and carry its erasure flag to both bits
	for (uint8_t i = 0; i < 98u; i++) {
		uint8_t p = interleave98(i);
		uint8_t e = erasures != NULL ? erasures[i] : 0u;

		dei[2u * p] = bits[2u * i];
		dei[2u * p + 1u] = bits[2u * i + 1u];
		era[2u * p] = e;
		era[2u * p + 1u] = e;
	}

	for (uint8_t s = 0; s < 49u; s++) {
		uint8_t nib = (uint8_t)((dei[4u * s] << 3) | (dei[4u * s + 1u] << 2) |
					(dei[4u * s + 2u] << 1) | dei[4u * s + 3u]);
		uint8_t em = (uint8_t)((era[4u * s] << 3) | (era[4u * s + 1u] << 2) |
				       (era[4u * s + 2u] << 1) | era[4u * s + 3u]);

		for (uint8_t nx = 0; nx < 4u; nx++) {
			uint32_t best = 0xFFFFFFFFu;
			uint8_t bp = 0;

			for (uint8_t pv = 0; pv < 4u; pv++) {
				// Score only the un-erased bits of the nibble
				uint8_t diff = (uint8_t)((nib ^ dtm[(pv << 2) | nx]) & (uint8_t)~em & 0xFu);
				uint32_t c = prev[pv] + hamming4(diff, 0u);

				if (c < best) {
					best = c;
					bp = pv;
				}
			}
			cur[nx] = best;
			back[s][nx] = bp;
		}
		memcpy(prev, cur, sizeof prev);
	}

	uint8_t st = 0;
	uint32_t metric = prev[0];

	for (uint8_t n = 1; n < 4u; n++) {
		if (prev[n] < metric) {
			metric = prev[n];
			st = n;
		}
	}
	for (int i = 48; i >= 0; i--) {
		states[i] = st;
		st = back[i][st];
	}
	for (uint8_t i = 0; i < P25_TSBK_BYTES; i++) {
		out[i] = (uint8_t)((states[4u * i] << 6) | (states[4u * i + 1u] << 4) |
				   (states[4u * i + 2u] << 2) | states[4u * i + 3u]);
	}

	uint8_t cb[80];

	for (uint8_t i = 0; i < 10u; i++) {
		for (uint8_t b = 0; b < 8u; b++) {
			cb[8u * i + b] = (uint8_t)((out[i] >> (7u - b)) & 1u);
		}
	}
	uint16_t tx = (uint16_t)((out[10] << 8) | out[11]);

	return crc16_ccitt(cb, 80) == tx ? (int)metric : -1;
}
