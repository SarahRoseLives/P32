// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 The RadioFoundry Authors
// P25 HDU and LDU2 encryption-sync fields: MI, ALGID 0xAA and the key ID, with the Golay/Hamming and RS parity
// recomputed (docs/spec/p25.md 9.1). Used by the conventional ADP transmitter.
#ifndef P25_ESS_H
#define P25_ESS_H

#include <stdint.h>
#include <stdbool.h>

// The base frame supplies the sync, NID, status symbols and the fields not in ESS. mi is the 9-byte ESS MI.
bool p25_ess_build_hdu(uint8_t out[99], const uint8_t base[99], const uint8_t mi[9], uint16_t group, uint16_t kid);
bool p25_ess_build_ldu2(uint8_t out[216], const uint8_t base[216], const uint8_t mi[9], uint16_t kid);

#endif
