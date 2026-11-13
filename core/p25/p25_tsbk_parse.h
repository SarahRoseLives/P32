// SPDX-License-Identifier: ISC
// Copyright (c) 2026 The RadioFoundry Authors
// P25 Phase 1 TSBK fields for a control-channel display (docs/spec/p25.md). The opcode and every field is read from
// the decoded 12-byte block; nothing depends on a particular system.
#ifndef RFOUNDRY_P25_TSBK_PARSE_H
#define RFOUNDRY_P25_TSBK_PARSE_H

#include <stdint.h>
#include <stdbool.h>

// The control-channel values a TSBK can carry; a field stays untouched when the block's opcode does not set it
typedef struct {
	uint8_t opcode;		// the TSBK opcode (the protected bit is not part of it)
	uint16_t nac;		// from the NID, not the TSBK
	uint32_t wacn;
	uint16_t sys;
	uint8_t rfss;
	uint8_t site;
	uint16_t tg;
	uint16_t vch;
	uint32_t freq_hz;	// from an IdenUp (the band's base)
	uint8_t iden_valid;	// bit i: IdenUp channel id i has been seen
	uint32_t base_hz[16];	// IdenUp base per channel id
	uint16_t spacing_hz[16];
	int32_t offset_hz[16];	// IdenUp subscriber transmit offset per channel id
	uint32_t voice_hz;	// the last grant's downlink channel on its band plan (0 unknown)
	uint32_t voice_tx_hz;	// the last grant's subscriber transmit channel (0 unknown)
	uint8_t voice_id;	// the last grant's IDEN (its band plan may arrive after the grant)
	bool voice_grant;	// a group voice grant has been seen (channel number may be 0)
	uint32_t source;	// the last grant's source unit
	uint32_t grant_seq;	// grants parsed so far (the radio follows a change)
	uint16_t affiliate_tg;	// the last Group Affiliation Response's group
	uint32_t affiliate_source;	// its source unit
	uint32_t affiliation_seq;	// affiliation responses parsed so far
} p25_cc_t;

// Decode one CRC-checked 12-byte TSBK into cc. Returns false for an opcode this parser does not know (cc->opcode is
// still set). Network Status, RFSS Status, IdenUp and Group Voice Grant are understood.
bool p25_tsbk_parse(const uint8_t tsbk[12], p25_cc_t *cc);

#endif
