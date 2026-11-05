// SPDX-License-Identifier: MIT
// Copyright (c) 2026 The RadioFoundry Authors
// P25 Phase 1 NID BCH(63,16,11): the code that protects the Network ID (docs/spec/p25.md).
#ifndef RFOUNDRY_P25_BCH_H
#define RFOUNDRY_P25_BCH_H

#include <stdint.h>

// The 16 information bits are the 12-bit NAC in bits 15:4 and the 4-bit DUID in bits 3:0. The codeword is systematic:
// the information in the high 16 bits and 47 parity bits below, so a clean codeword is (info << 47) | parity.
#define P25_BCH_INFO_BITS	16
#define P25_BCH_CODE_BITS	63

// One codeword for info, 63 bits (bit 62 is transmitted first).
uint64_t p25_bch_encode(uint16_t info);

// Decode one 63-bit codeword, correcting up to 11 bit errors. Returns the number of corrected bits and writes the
// information to *info, or -1 when the word is beyond the code's correction radius (or does not converge).
int p25_bch_decode(uint64_t cw, uint16_t *info);

#endif
