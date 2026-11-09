// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 The RadioFoundry Authors
//
// P25 Phase 1 trunked control-channel transmit: TSBK construction (CRC-16 CCITT, 1/2-rate trellis and the 98-dibit
// interleave) and the 180-dibit control-channel frame (FSW, NID and TSCC status symbols) (docs/spec/p25.md 7.2).
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
#ifndef RFOUNDRY_P25_CC_TX_H
#define RFOUNDRY_P25_CC_TX_H

#include <stdint.h>

#define P25_CC_TSBK_BYTES	12	// the 10-byte message and the CRC-16
#define P25_CC_FRAME_DIBITS	180	// FSW, NID, TSBK and the five TSCC status symbols
#define P25_CC_FRAME_BYTES	45	// the dibits packed four to a byte

// Build one TSBK from its opcode and 64-bit argument field: byte 0 is 0x80 | opcode, bytes 2..9 are args (big-endian)
// and bytes 10..11 are the CRC-16 CCITT over the first 80 bits.
void p25_tsbk_build(uint8_t out[P25_CC_TSBK_BYTES], uint8_t opcode, uint64_t args);

// Unit Registration Request (opcode 0x2C). Byte 2 is the emergency bit (7) and the capability (6..0), the 20-bit WACN
// and 12-bit system ID share bytes 3..6 and the 24-bit source ID is bytes 7..9.
void p25_tsbk_unit_registration(uint8_t out[P25_CC_TSBK_BYTES], uint32_t wacn, uint16_t sys, uint32_t source,
				uint8_t emergency, uint8_t capability);

// Group Affiliation Response/Request (opcode 0x28). Byte 2 is reserved (0), the 12-bit system ID is byte 3's low
// nibble and byte 4, the 16-bit group is bytes 5..6 and the 24-bit source ID is bytes 7..9.
void p25_tsbk_group_affiliation(uint8_t out[P25_CC_TSBK_BYTES], uint16_t sys, uint16_t group, uint32_t source);

// Group Voice Channel Request (opcode 0x00). Byte 2 is the service options (bit 7 emergency), bytes 3..4 are zero,
// the 16-bit group is bytes 5..6 and the 24-bit source ID is bytes 7..9.
void p25_tsbk_group_voice_request(uint8_t out[P25_CC_TSBK_BYTES], uint16_t group, uint32_t source, uint8_t options);

// Unit De-Registration Request (opcode 0x2B; 0x2F is the site's outbound ack) with the 24-bit source in bytes 7..9.
void p25_tsbk_unit_deregistration(uint8_t out[P25_CC_TSBK_BYTES], uint32_t source);
// The older SiteSim/dsd spelling of the same inbound message (opcode 0x2F). Sent alongside 0x2B for compatibility.
void p25_tsbk_unit_deregistration_ack(uint8_t out[P25_CC_TSBK_BYTES], uint32_t source);

// Group Affiliation Query Response (ISP 0x29), the answer to the site's GRP_AFF_Q: group in bytes 5..6 and source in
// bytes 7..9. It makes the site's affiliation check current before the voice request.
void p25_tsbk_group_affiliation_query_response(uint8_t out[P25_CC_TSBK_BYTES], uint16_t group, uint32_t source);

// Build the frame as 180 dibits: 24 FSW, 32 NID, the 98 trellis and interleaved TSBK dibits, then one TSCC status
// symbol (0x02) after every 35 data dibits with zero padding to the last one.
void p25_cc_build_frame_dibits(uint8_t out[P25_CC_FRAME_DIBITS], uint16_t nac, const uint8_t tsbk[P25_CC_TSBK_BYTES]);

// The same frame packed for the PHY byte stream.
void p25_cc_build_frame(uint8_t out[P25_CC_FRAME_BYTES], uint16_t nac, const uint8_t tsbk[P25_CC_TSBK_BYTES]);

#endif
