// SPDX-License-Identifier: MIT
// Copyright (c) 2026 The RadioFoundry Authors
// P25 Phase 1 physical layer receive: frame-sync search, status-symbol removal and the Network ID, from a stream of
// sliced C4FM symbols (docs/spec/p25.md). Fed by whatever demodulates the 4800 baud 4FSK: a chip's raw-symbol mode,
// or a software demodulator.
#ifndef RFOUNDRY_P25_PHY_H
#define RFOUNDRY_P25_PHY_H

#include <stdbool.h>
#include <stdint.h>

// C4FM symbol values as TIA-102.BAAA sends them: the four deviations -3, -1, +1, +3 (x 600 Hz). The dibit mapping is
// fixed: -3 -> 11, -1 -> 10, +1 -> 00, +3 -> 01, first bit transmitted first.
#define P25_SYMBOL_M3		(-3)
#define P25_SYMBOL_M1		(-1)
#define P25_SYMBOL_P1		1
#define P25_SYMBOL_P3		3

// Frame sync word, 0x5575F5FF77FF as 24 dibits
#define P25_SYNC_DIBITS		24
// One 2-bit status symbol rides after every 35 data dibits, counting from the sync's first dibit
#define P25_STATUS_STRIDE	36
#define P25_STATUS_FIRST	35
// The NID is 64 bits: a 63-bit BCH codeword and the per-DUID flag bit
#define P25_NID_BITS		64
#define P25_NID_DIBITS		(P25_NID_BITS / 2)

// A 4-bit Data Unit ID, protected with the NAC in the NID
typedef enum {
	P25_DUID_HDU		= 0x0,	// voice call header
	P25_DUID_TDU		= 0x3,	// terminator
	P25_DUID_LDU1		= 0x5,	// voice, link control
	P25_DUID_TSDU		= 0x7,	// trunking signalling (control channel)
	P25_DUID_LDU2		= 0xA,	// voice, encryption sync
	P25_DUID_PDU		= 0xC,	// packet data
	P25_DUID_TDULC		= 0xF,	// terminator with link control
} p25_duid_t;

typedef struct {
	uint16_t nac;
	uint8_t duid;
	uint8_t errors;		// bit errors the BCH code corrected
	bool flag_ok;		// the 64th bit matches the DUID (1 for LDU1/LDU2, 0 otherwise)
} p25_nid_t;

typedef void (*p25_nid_cb_t)(void *ctx, const p25_nid_t *nid);

typedef struct {
	p25_nid_cb_t cb;
	void *ctx;
	uint8_t hist[P25_SYNC_DIBITS];	// the last 24 dibits, newest last
	uint8_t hist_len;
	uint8_t rot;			// alphabet rotation found with the sync (0 or 2)
	bool synced;
	uint16_t rel;			// on-air symbols since the sync's first
	uint64_t nid;			// NID bits collected so far, first bit in bit 63
	uint8_t nid_bits;
} p25_phy_t;

void p25_phy_init(p25_phy_t *p, p25_nid_cb_t cb, void *ctx);

// One sliced symbol, -3 / -1 / +1 / +3. Any other value is ignored (a slicer gap).
void p25_phy_push_symbol(p25_phy_t *p, int8_t symbol);

// One dibit with the TIA mapping above (0..3). Use this when the receiver already yields dibits.
void p25_phy_push_dibit(p25_phy_t *p, uint8_t dibit);

#endif
