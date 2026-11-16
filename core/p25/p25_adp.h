// SPDX-License-Identifier: MIT
// Copyright (c) 2026 The RadioFoundry Authors
// Motorola ADP (P25 ALGID 0xAA) keystream: RC4 over the 5-byte key and the first 8 bytes of the MI, after the first
// 256 keystream bytes are discarded. The encrypted IMBE payloads are FEC-coded on air, so callers decrypt after
// p25_rx_voice_quality() and encrypt before p25_live_ldu_template(). See docs/spec/p25.md 9.1.
#ifndef RFOUNDRY_P25_ADP_H
#define RFOUNDRY_P25_ADP_H

#include <stdbool.h>
#include <stdint.h>

#define P25_ADP_KEY_LEN		5
#define P25_ADP_MI_LEN		8
#define P25_ADP_STREAM_LEN	320	// two LDUs: LDU2's last frame starts at 112 + 8 * 11
#define P25_ADP_LDU1_OFFSET	11
#define P25_ADP_LDU2_OFFSET	112

// The keystream for one superframe. mi is the 9-byte ESS MI; only its first 8 bytes key the cipher.
void p25_adp_keystream(const uint8_t key[P25_ADP_KEY_LEN], const uint8_t mi[9], uint8_t out[P25_ADP_STREAM_LEN]);
// XOR one 11-byte IMBE payload with the keystream at off + 11 * frame. The same call decrypts and encrypts.
void p25_adp_apply(uint8_t payload[11], const uint8_t stream[P25_ADP_STREAM_LEN], unsigned off, unsigned frame);
// Generic RC4 for the host test's known-answer vector; the ADP path uses p25_adp_keystream().
void p25_adp_rc4(const uint8_t *key, unsigned key_len, uint8_t *out, unsigned len, unsigned drop);

#endif
