// SPDX-License-Identifier: ISC
// Copyright (c) 2026 The RadioFoundry Authors
// The TSBK's first byte is the protected bit and the opcode; bytes 2..9 are the 64-bit argument field, big-endian.
#include "p25_tsbk_parse.h"

static uint64_t args64(const uint8_t t[12])
{
	uint64_t v = 0;

	for (uint8_t i = 2; i < 10; i++) {
		v = (v << 8) | t[i];
	}
	return v;
}

bool p25_tsbk_parse(const uint8_t tsbk[12], p25_cc_t *cc)
{
	uint64_t a = args64(tsbk);

	cc->opcode = (uint8_t)(tsbk[0] & 0x7Fu);
	switch (cc->opcode) {
	case 0x3B:		// Network Status Broadcast: WACN and system ID
		cc->wacn = (uint32_t)((a >> 36) & 0xFFFFFu);
		cc->sys = (uint16_t)((a >> 24) & 0xFFFu);
		return true;
	case 0x3A:		// RFSS Status Broadcast: system, RFSS and site
		cc->sys = (uint16_t)((a >> 40) & 0xFFFu);
		cc->rfss = (uint8_t)((a >> 32) & 0xFFu);
		cc->site = (uint8_t)((a >> 24) & 0xFFu);
		return true;
	case 0x3D: {	// Identifier Update: channel id, bandwidth, offset, spacing, base
		unsigned id = (unsigned)((a >> 60) & 0xFu);

		cc->base_hz[id] = (uint32_t)(a & 0xFFFFFFFFu) * 5u;
		cc->spacing_hz[id] = (uint16_t)(((a >> 32) & 0x3FFu) * 125u);
		cc->iden_valid = (uint8_t)(cc->iden_valid | (1u << id));
		cc->freq_hz = cc->base_hz[id];
		return true;
	}
	case 0x00: {		// Group Voice Grant: channel id, channel and talkgroup
		uint32_t ch = (uint32_t)((a >> 40) & 0xFFFFu);
		unsigned id = (unsigned)((ch >> 12) & 0xFu);

		cc->vch = (uint16_t)(ch & 0xFFFu);
		cc->tg = (uint16_t)((a >> 24) & 0xFFFFu);
		cc->voice_hz = (cc->iden_valid & (1u << id)) != 0
			? cc->base_hz[id] + (uint32_t)cc->vch * cc->spacing_hz[id] : 0u;
		cc->grant_seq++;
		return true;
	}
	default:
		return false;
	}
}
