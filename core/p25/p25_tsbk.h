// SPDX-License-Identifier: ISC
// Copyright (c) 2026 The RadioFoundry Authors
// P25 Phase 1 TSBK: the 1/2-rate trellis, the 98-dibit interleaver and the CRC-16 (docs/spec/p25.md).
// The trellis transition set and the CRC follow dsd-neo's p25_12.c / p25_crc.c (ISC, Copyright (C) 2026
// arancormonk <180709949+arancormonk@users.noreply.github.com>); the interleaver is the standard's, implemented
// from its pattern.
#ifndef RFOUNDRY_P25_TSBK_H
#define RFOUNDRY_P25_TSBK_H

#include <stdint.h>

#define P25_TSBK_BITS	196	// 98 dibits, the 196 bits after the NID with the status symbols removed
#define P25_TSBK_BYTES	12	// 10 message bytes and the CRC-16

// Decode one 196-bit TSBK block into its 12 bytes. Returns the Viterbi metric (the corrected bits, 0 on a clean
// block) or -1 when the CRC-16 does not check.
int p25_tsbk_decode(const uint8_t bits[P25_TSBK_BITS], uint8_t out[P25_TSBK_BYTES]);

#endif
