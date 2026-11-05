// SPDX-License-Identifier: MIT
// Copyright (c) 2026 The RadioFoundry Authors
// P25 Phase 1 physical layer receive (docs/spec/p25.md): find the 48-bit frame sync, drop the status symbols, read
// the 64-bit NID and BCH-decode it. The hard parts are not obvious from the standard:
//   - the sync is 24 dibits and the alphabet may arrive polarity-flipped (rotation 2), so both rotations are tried;
//   - a 2-bit status symbol replaces every 36th on-air dibit (the first at dibit 35, counted from the sync's first),
//     so the NID's dibits are not the 32 that follow the sync on air.
#include <string.h>
#include "p25_phy.h"
#include "p25_bch.h"

static const uint8_t fsw[P25_SYNC_DIBITS] = {
	1, 1, 1, 1, 1, 3, 1, 1, 3, 3, 1, 1, 3, 3, 3, 3, 1, 3, 1, 3, 3, 3, 3, 3,
};

// Mismatches after undoing a rotation: (received + rot) mod 4 is the canonical alphabet
static int fsw_mismatch(const uint8_t *h, uint8_t rot)
{
	int n = 0;

	for (int i = 0; i < P25_SYNC_DIBITS; i++) {
		if (((h[i] + rot) & 3u) != fsw[i]) {
			n++;
		}
	}
	return n;
}

static int sym_to_dibit(int8_t symbol)
{
	switch (symbol) {
	case P25_SYMBOL_M3:
		return 3;
	case P25_SYMBOL_M1:
		return 2;
	case P25_SYMBOL_P1:
		return 0;
	case P25_SYMBOL_P3:
		return 1;
	default:
		return -1;
	}
}

void p25_phy_init(p25_phy_t *p, p25_nid_cb_t cb, void *ctx)
{
	memset(p, 0, sizeof *p);
	p->cb = cb;
	p->ctx = ctx;
}

void p25_phy_push_symbol(p25_phy_t *p, int8_t symbol)
{
	int dibit = sym_to_dibit(symbol);

	if (dibit >= 0) {
		p25_phy_push_dibit(p, (uint8_t)dibit);
	}
}

void p25_phy_push_dibit(p25_phy_t *p, uint8_t dibit)
{
	if (dibit > 3) {
		return;
	}

	if (!p->synced) {
		if (p->hist_len < P25_SYNC_DIBITS) {
			p->hist[p->hist_len++] = dibit;
			if (p->hist_len < P25_SYNC_DIBITS) {
				return;
			}
		} else {
			memmove(p->hist, &p->hist[1], P25_SYNC_DIBITS - 1);
			p->hist[P25_SYNC_DIBITS - 1] = dibit;
		}
		int m0 = fsw_mismatch(p->hist, 0);
		int m2 = fsw_mismatch(p->hist, 2);
		int m = m2 < m0 ? m2 : m0;
		// A sync hit: the 24 dibits just seen start a frame and the NID follows
		if (m <= 4) {
			p->synced = true;
			p->rot = m2 < m0 ? 2 : 0;
			p->rel = P25_SYNC_DIBITS;
			p->nid = 0;
			p->nid_bits = 0;
		}
		return;
	}

	// The status symbols sit at dibit 35, 71, ... counted from the sync's first dibit and carry no NID bit
	if (p->rel >= P25_STATUS_FIRST &&
	    ((p->rel - P25_STATUS_FIRST) % P25_STATUS_STRIDE) == 0) {
		p->rel++;
		return;
	}

	p->nid = (p->nid << 2) | ((dibit + p->rot) & 3u);
	p->nid_bits += 2;
	p->rel++;
	if (p->nid_bits < P25_NID_BITS) {
		return;
	}

	uint16_t info;
	int errors = p25_bch_decode(p->nid >> 1, &info);
	if (errors >= 0 && p->cb != NULL) {
		p25_nid_t nid = {
			.nac = (uint16_t)(info >> 4),
			.duid = (uint8_t)(info & 0xF),
			.errors = (uint8_t)errors,
			.flag_ok = ((p->nid & 1u) ==
				    ((info & 0xF) == P25_DUID_LDU1 || (info & 0xF) == P25_DUID_LDU2 ? 1u : 0u)),
		};
		p->cb(p->ctx, &nid);
	}

	// Scan again: the history no longer holds the sync, so rebuild it from the next 24 dibits
	p->synced = false;
	p->hist_len = 0;
}
