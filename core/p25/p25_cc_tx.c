// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 The RadioFoundry Authors
//
// P25 Phase 1 trunked control-channel transmit: TSBK construction (CRC-16 CCITT, 1/2-rate trellis and the 98-dibit
// interleave) and the 180-dibit control-channel frame (FSW, NID and TSCC status symbols) (docs/spec/p25.md 7.2).
// The decoder side is core/p25/p25_tsbk.c; an encoded block must survive p25_tsbk_decode.
//
// Ported from the PortaPack Mayhem P25 TX app, firmware/application/external/p25_tx/ui_p25_tx.cpp (GPL):
//   Copyright (C) 2025 Sarah Rose
//
//   This program is free software; you can redistribute it and/or modify it under the terms of the GNU General
//   Public License as published by the Free Software Foundation; either version 2, or (at your option) any later
//   version.
//
//   This program is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the
//   implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License
//   for more details.
//
//   You should have received a copy of the GNU General Public License along with this program; see the file
//   COPYING.  If not, write to the Free Software Foundation, Inc., 51 Franklin Street, Boston, MA 02110-1301, USA.
#include <string.h>
#include <stdbool.h>
#include "p25_cc_tx.h"
#include "p25_bch.h"
#include "p25_tsbk.h"

#define P25_CC_FSW		0x5575F5FF77FFull
#define P25_CC_DUID_TSDU	0x7u
#define P25_CC_STATUS_TSCC	0x02u
#define P25_CC_STATUS_SLOT_START	0x03u
#define P25_CC_DATA_DIBITS	(24 + 32 + P25_TSBK_DIBITS)

// [state][input dibit] = the two output dibits
static const uint8_t trellis_table[4][4][2] = {
	{ { 0, 2 }, { 3, 0 }, { 0, 1 }, { 3, 3 } },
	{ { 3, 2 }, { 0, 0 }, { 3, 1 }, { 0, 3 } },
	{ { 2, 1 }, { 1, 3 }, { 2, 2 }, { 1, 0 } },
	{ { 1, 1 }, { 2, 3 }, { 1, 2 }, { 2, 0 } },
};

// CRC-16 CCITT over the 80 message bits, MSB first: init 0, polynomial 0x1021, final complement (p25_tsbk.c)
static uint16_t crc_ccitt_80(const uint8_t msg[10])
{
	uint16_t crc = 0;

	for (int i = 0; i < 80; i++) {
		uint8_t bit = (uint8_t)((msg[i / 8] >> (7 - (i % 8))) & 1u);
		uint8_t mix = (uint8_t)(((crc >> 15) & 1u) ^ bit);

		crc = (uint16_t)(crc << 1);
		if (mix) {
			crc ^= 0x1021u;
		}
	}
	return (uint16_t)(crc ^ 0xFFFFu);
}

static void tsbk_build_ex(uint8_t out[P25_CC_TSBK_BYTES], uint8_t opcode, uint64_t args, bool protect)
{
	uint8_t msg[10];
	uint16_t crc;

	msg[0] = (uint8_t)(0x80u | (protect ? 0x40u : 0u) | (opcode & 0x3Fu));
	msg[1] = 0u;
	for (int i = 0; i < 8; i++) {
		msg[2 + i] = (uint8_t)(args >> (56 - 8 * i));
	}
	crc = crc_ccitt_80(msg);
	memcpy(out, msg, 10);
	out[10] = (uint8_t)(crc >> 8);
	out[11] = (uint8_t)crc;
}

void p25_tsbk_build(uint8_t out[P25_CC_TSBK_BYTES], uint8_t opcode, uint64_t args)
{
	tsbk_build_ex(out, opcode, args, false);
}

// Pack the eight argument bytes into the 64-bit field p25_tsbk_build takes (bytes 2..9, big-endian)
static uint64_t pack_args(const uint8_t msg[8])
{
	uint64_t args = 0;

	for (int i = 0; i < 8; i++) {
		args = (args << 8) | msg[i];
	}
	return args;
}

void p25_tsbk_unit_registration(uint8_t out[P25_CC_TSBK_BYTES], uint32_t wacn, uint16_t sys, uint32_t source,
				uint8_t emergency, uint8_t capability)
{
	uint8_t msg[8];		// TSBK bytes 2..9

	msg[0] = (uint8_t)((emergency != 0u ? 0x80u : 0u) | (capability & 0x7Fu));
	msg[1] = (uint8_t)((wacn >> 12) & 0xFFu);
	msg[2] = (uint8_t)((wacn >> 4) & 0xFFu);
	msg[3] = (uint8_t)(((wacn & 0xFu) << 4) | ((sys >> 8) & 0xFu));
	msg[4] = (uint8_t)(sys & 0xFFu);
	msg[5] = (uint8_t)((source >> 16) & 0xFFu);
	msg[6] = (uint8_t)((source >> 8) & 0xFFu);
	msg[7] = (uint8_t)(source & 0xFFu);
	p25_tsbk_build(out, 0x2Cu, pack_args(msg));
}

void p25_tsbk_group_affiliation(uint8_t out[P25_CC_TSBK_BYTES], uint16_t sys, uint16_t group, uint32_t source)
{
	uint8_t msg[8];		// TSBK bytes 2..9

	msg[0] = 0u;
	msg[1] = (uint8_t)((sys >> 8) & 0xFu);
	msg[2] = (uint8_t)(sys & 0xFFu);
	msg[3] = (uint8_t)((group >> 8) & 0xFFu);
	msg[4] = (uint8_t)(group & 0xFFu);
	msg[5] = (uint8_t)((source >> 16) & 0xFFu);
	msg[6] = (uint8_t)((source >> 8) & 0xFFu);
	msg[7] = (uint8_t)(source & 0xFFu);
	p25_tsbk_build(out, 0x28u, pack_args(msg));
}

void p25_tsbk_group_voice_request(uint8_t out[P25_CC_TSBK_BYTES], uint16_t group, uint32_t source, uint8_t options)
{
	uint8_t msg[8];		// TSBK bytes 2..9

	msg[0] = options;
	msg[1] = 0u;
	msg[2] = 0u;
	msg[3] = (uint8_t)((group >> 8) & 0xFFu);
	msg[4] = (uint8_t)(group & 0xFFu);
	msg[5] = (uint8_t)((source >> 16) & 0xFFu);
	msg[6] = (uint8_t)((source >> 8) & 0xFFu);
	msg[7] = (uint8_t)(source & 0xFFu);
	p25_tsbk_build(out, 0x00u, pack_args(msg));
}

void p25_tsbk_group_affiliation_query_response(uint8_t out[P25_CC_TSBK_BYTES], uint16_t group, uint32_t source)
{
	uint8_t msg[8] = { 0 };

	msg[3] = (uint8_t)((group >> 8) & 0xFFu);
	msg[4] = (uint8_t)(group & 0xFFu);
	msg[5] = (uint8_t)((source >> 16) & 0xFFu);
	msg[6] = (uint8_t)((source >> 8) & 0xFFu);
	msg[7] = (uint8_t)(source & 0xFFu);
	p25_tsbk_build(out, 0x29u, pack_args(msg));
}

void p25_tsbk_unit_deregistration(uint8_t out[P25_CC_TSBK_BYTES], uint32_t source)
{
	uint8_t msg[8] = { 0 };

	msg[5] = (uint8_t)((source >> 16) & 0xFFu);
	msg[6] = (uint8_t)((source >> 8) & 0xFFu);
	msg[7] = (uint8_t)(source & 0xFFu);
	p25_tsbk_build(out, 0x2Bu, pack_args(msg));
}

void p25_tsbk_unit_deregistration_ack(uint8_t out[P25_CC_TSBK_BYTES], uint32_t source)
{
	uint8_t msg[8] = { 0 };

	msg[5] = (uint8_t)((source >> 16) & 0xFFu);
	msg[6] = (uint8_t)((source >> 8) & 0xFFu);
	msg[7] = (uint8_t)(source & 0xFFu);
	p25_tsbk_build(out, 0x2Fu, pack_args(msg));
}

static void trellis_encode(const uint8_t in48[48], uint8_t out98[P25_TSBK_DIBITS])
{
	uint8_t state = 0;

	for (int i = 0; i < 49; i++) {
		uint8_t d = i < 48 ? in48[i] : 0u;	// the final zero symbol flushes the trellis

		out98[2 * i] = trellis_table[state][d][0];
		out98[2 * i + 1] = trellis_table[state][d][1];
		state = d;
	}
}

// The standard's 98-dibit interleaver: four offset groups of eight, 13 + 12 + 12 + 12 pairs
static void data_interleave(const uint8_t in98[P25_TSBK_DIBITS], uint8_t out98[P25_TSBK_DIBITS])
{
	int idx = 0;

	for (int j = 0; j < 97; j += 8) {
		out98[idx++] = in98[j];
		out98[idx++] = in98[j + 1];
	}
	for (int i = 2; i < 7; i += 2) {
		for (int j = 0; j < 89; j += 8) {
			out98[idx++] = in98[i + j];
			out98[idx++] = in98[i + j + 1];
		}
	}
}

// One status symbol after every 35 data dibits, counting from the FSW's first, then zero padding to the last status.
// The 154 data dibits take four statuses inside them and a fifth after the padding, for 180 dibits.
static void insert_status(const uint8_t *data, int dlen, uint8_t *out, uint8_t ss)
{
	int num_ss = (dlen + 34) / 35;
	int remaining = num_ss;
	int ss_index = 0;
	int oi = 0;
	int i = 1;

	for (int d = 0; d < dlen; d++) {
		out[oi++] = data[d];
		if ((i % 35 == 0) && remaining > 0) {
			out[oi++] = ss_index++ == 0 ? P25_CC_STATUS_SLOT_START : ss;
			remaining--;
		}
		i++;
	}
	while (remaining > 0) {
		out[oi++] = 0;
		if (i % 35 == 0) {
			out[oi++] = ss_index++ == 0 ? P25_CC_STATUS_SLOT_START : ss;
			remaining--;
		}
		i++;
	}
}

void p25_cc_build_frame_dibits(uint8_t out[P25_CC_FRAME_DIBITS], uint16_t nac, const uint8_t tsbk[P25_CC_TSBK_BYTES])
{
	uint8_t data[P25_CC_DATA_DIBITS];
	uint8_t in_d[48];
	uint8_t enc[P25_TSBK_DIBITS];
	uint8_t ilv[P25_TSBK_DIBITS];
	uint64_t nid;
	int idx = 0;

	for (int i = 0; i < 24; i++) {
		data[idx++] = (uint8_t)((P25_CC_FSW >> (46 - 2 * i)) & 3u);
	}

	// The NID is the 63-bit BCH codeword, a TSDU's 64th (flag) bit is 0. p25_bch_encode puts the 16 information
	// bits at 62..47, so shift the codeword left one to place them at 63..48 and the flag at bit 0.
	nid = (uint64_t)p25_bch_encode((uint16_t)((nac << 4) | P25_CC_DUID_TSDU)) << 1;
	for (int i = 0; i < 32; i++) {
		data[idx++] = (uint8_t)((nid >> (62 - 2 * i)) & 3u);
	}

	for (int i = 0; i < P25_CC_TSBK_BYTES; i++) {
		in_d[4 * i + 0] = (uint8_t)((tsbk[i] >> 6) & 3u);
		in_d[4 * i + 1] = (uint8_t)((tsbk[i] >> 4) & 3u);
		in_d[4 * i + 2] = (uint8_t)((tsbk[i] >> 2) & 3u);
		in_d[4 * i + 3] = (uint8_t)(tsbk[i] & 3u);
	}
	trellis_encode(in_d, enc);
	data_interleave(enc, ilv);
	memcpy(&data[idx], ilv, P25_TSBK_DIBITS);
	idx += P25_TSBK_DIBITS;

	insert_status(data, idx, out, P25_CC_STATUS_TSCC);
}

// Pack four dibits per byte, most significant first: bits 7..6 hold the first dibit of the group and bits 1..0 the
// fourth. Each 45-byte frame is 180 dibits in transmission order.
void p25_cc_build_frame(uint8_t out[P25_CC_FRAME_BYTES], uint16_t nac, const uint8_t tsbk[P25_CC_TSBK_BYTES])
{
	uint8_t dibits[P25_CC_FRAME_DIBITS];

	p25_cc_build_frame_dibits(dibits, nac, tsbk);
	for (int i = 0; i < P25_CC_FRAME_BYTES; i++) {
		out[i] = (uint8_t)((dibits[4 * i] << 6) | (dibits[4 * i + 1] << 4) |
				   (dibits[4 * i + 2] << 2) | dibits[4 * i + 3]);
	}
}
