// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 The RadioFoundry Authors
// HDU/LDU2 ESS encoder. The field layout and the FEC conventions are the reverse of core/p25/p25_call.c: a systematic
// 20/16 RS(36,20) or 16/8 RS(24,16) codeword over GF(64), each symbol protected by Golay(18,6) (HDU) or Hamming(10,6)
// (LDU2), placed at the coded frame's data-bit positions (status symbols are skipped).
#include "p25_ess.h"

#include <string.h>

static const uint8_t gf_exp[63] = {
	1, 2, 4, 8, 16, 32, 3, 6, 12, 24, 48, 35, 5, 10, 20, 40, 19, 38, 15, 30, 60,
	59, 53, 41, 17, 34, 7, 14, 28, 56, 51, 37, 9, 18, 36, 11, 22, 44, 27, 54, 47,
	29, 58, 55, 45, 25, 50, 39, 13, 26, 52, 43, 21, 42, 23, 46, 31, 62, 63, 61, 57, 49, 33
};
static const uint8_t gf_log[64] = {
	0, 0, 1, 6, 2, 12, 7, 26, 3, 32, 13, 35, 8, 48, 27, 18, 4, 24, 33, 16, 14,
	52, 36, 54, 9, 45, 49, 38, 28, 41, 19, 56, 5, 62, 25, 11, 34, 31, 17, 47,
	15, 23, 53, 51, 37, 44, 55, 40, 10, 61, 46, 30, 50, 22, 39, 43, 29, 60, 42, 21, 20, 59, 57, 58
};

static uint8_t gf_mul(uint8_t a, uint8_t b)
{
	if (!a || !b)
		return 0;
	unsigned i = (unsigned)gf_log[a] + gf_log[b];
	if (i >= 63)
		i -= 63;
	return gf_exp[i];
}

static uint8_t gf_div(uint8_t a, uint8_t b)
{
	if (!a)
		return 0;
	unsigned i = (unsigned)gf_log[a] + 63u - gf_log[b];
	if (i >= 63)
		i -= 63;
	return gf_exp[i];
}

static uint8_t gf_pow(uint8_t a, unsigned e)
{
	uint8_t v = 1;
	while (e--) v = gf_mul(v, a);
	return v;
}

static uint32_t golay18(uint32_t data)
{
	static const uint32_t g[6] = { 0x206cd, 0x10367, 0x08dc6, 0x04a97, 0x0293e, 0x018eb };
	uint32_t v = 0;
	for (unsigned i = 0; i < 6; ++i)
		if (data & (32u >> i))
			v ^= g[i];
	return v;
}

static uint32_t hamming10(uint32_t data)
{
	static const uint16_t g[6] = { 0x20e, 0x10d, 0x08b, 0x047, 0x023, 0x01c };
	uint32_t v = 0;
	for (unsigned i = 0; i < 6; ++i)
		if (data & (32u >> i))
			v ^= g[i];
	return v;
}

// Systematic RS parity: the last `parity` symbols make the codeword evaluate to zero at alpha^1..alpha^parity.
static void rs_parity(const uint8_t *data, unsigned ndata, unsigned parity, uint8_t *out)
{
	uint8_t m[16][17];

	for (unsigned r = 0; r < parity; r++) {
		uint8_t x = gf_exp[r + 1];
		uint8_t v = 0;
		for (unsigned i = 0; i < ndata; i++)
			v = (uint8_t)(gf_mul(v, x) ^ data[i]);
		for (unsigned i = 0; i < parity; i++)
			v = gf_mul(v, x);		// the parity symbols are appended: data(x) * x^parity
		for (unsigned c = 0; c < parity; c++)
			m[r][c] = gf_pow(x, parity - 1u - c);
		m[r][parity] = v;
	}
	for (unsigned c = 0; c < parity; c++) {
		unsigned p = c;
		while (p < parity && m[p][c] == 0)
			p++;
		if (p == parity) {
			memset(out, 0, parity);
			return;		// cannot happen with the fields above
		}
		if (p != c) {
			for (unsigned j = c; j <= parity; j++) {
				uint8_t t = m[c][j];
				m[c][j] = m[p][j];
				m[p][j] = t;
			}
		}
		uint8_t d = m[c][c];
		for (unsigned j = c; j <= parity; j++)
			m[c][j] = gf_div(m[c][j], d);
		for (unsigned r = 0; r < parity; r++) {
			if (r == c)
				continue;
			uint8_t f = m[r][c];
			if (!f)
				continue;
			for (unsigned j = c; j <= parity; j++)
				m[r][j] ^= gf_mul(f, m[c][j]);
		}
	}
	for (unsigned c = 0; c < parity; c++)
		out[c] = m[c][parity];
}

static void pack6(const uint8_t *bytes, unsigned count, uint8_t *symbols)
{
	for (unsigned i = 0; i < count; i++) {
		unsigned bit = i * 6u;
		uint8_t v = 0;
		for (unsigned j = 0; j < 6; j++)
			v = (uint8_t)((v << 1) | ((bytes[(bit + j) / 8u] >> (7u - (bit + j) % 8u)) & 1u));
		symbols[i] = v;
	}
}

// The frame's data-bit positions: status symbols are skipped (p25_call.c data_bit).
static void put_data_bit(uint8_t *frame, unsigned pos, unsigned bit)
{
	unsigned onair = pos + 2u * (pos / 70u);
	uint8_t mask = (uint8_t)(1u << (7u - onair % 8u));
	if (bit)
		frame[onair / 8u] |= mask;
	else
		frame[onair / 8u] &= (uint8_t)~mask;
}

static void put_code(uint8_t *frame, unsigned start, unsigned bits, uint32_t code)
{
	for (unsigned j = 0; j < bits; j++)
		put_data_bit(frame, start + j, (code >> (bits - 1u - j)) & 1u);
}

bool p25_ess_build_hdu(uint8_t out[99], const uint8_t base[99], const uint8_t mi[9], uint16_t group, uint16_t kid)
{
	uint8_t bytes[15], data[20], parity[16];

	memcpy(out, base, 99);
	memcpy(bytes, mi, 9);
	bytes[9] = 0;			// MFID
	bytes[10] = 0xAA;		// ADP
	bytes[11] = (uint8_t)(kid >> 8);
	bytes[12] = (uint8_t)kid;
	bytes[13] = (uint8_t)(group >> 8);
	bytes[14] = (uint8_t)group;
	pack6(bytes, 20, data);
	rs_parity(data, 20, 16, parity);
	for (unsigned i = 0; i < 20; i++)
		put_code(out, 112u + i * 18u, 18, golay18(data[i]));
	for (unsigned i = 0; i < 16; i++)
		put_code(out, 112u + (20u + i) * 18u, 18, golay18(parity[i]));
	return true;
}

bool p25_ess_build_ldu2(uint8_t out[216], const uint8_t base[216], const uint8_t mi[9], uint16_t kid)
{
	uint8_t bytes[12], data[16], parity[8];

	memcpy(out, base, 216);
	memcpy(bytes, mi, 9);
	bytes[9] = 0xAA;		// ADP
	bytes[10] = (uint8_t)(kid >> 8);
	bytes[11] = (uint8_t)kid;
	pack6(bytes, 16, data);
	rs_parity(data, 16, 8, parity);
	for (unsigned i = 0; i < 16; i++)
		put_code(out, 400u + (i / 4u) * 184u + (i % 4u) * 10u, 10, hamming10(data[i]));
	for (unsigned i = 0; i < 8; i++) {
		unsigned s = 16u + i;
		put_code(out, 400u + (s / 4u) * 184u + (s % 4u) * 10u, 10, hamming10(parity[i]));
	}
	return true;
}
