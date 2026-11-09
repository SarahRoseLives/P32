// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 The RadioFoundry Authors
// P25 Phase 1 IMBE voice decode: the 88-bit IMBE codeword of one 20 ms frame to 160 PCM samples, over the fixed-point
// vocoder (Pavel Yazev, GPL-3.0-or-later) (docs/spec/p25.md 6). The LDU deinterleave/FEC that produces the 88 bits is
// separate (p25_voice.c).
#ifndef RFOUNDRY_P25_IMBE_H
#define RFOUNDRY_P25_IMBE_H

#include <stdint.h>

#define P25_IMBE_BYTES		11	// 88 bits, MSB first: the packed IMBE codeword
#define P25_IMBE_SAMPLES	160	// 8 kHz, 20 ms

#if CONFIG_P25_VOICE
#ifdef __cplusplus
extern "C" {
#endif
// Clear the codec history (the first frame of a call, RX or TX).
void p25_imbe_reset(void);
// Decode one 88-bit codeword into 160 PCM samples.
void p25_imbe_decode(const uint8_t frame[P25_IMBE_BYTES], int16_t pcm[P25_IMBE_SAMPLES]);
#if CONFIG_P25_TX
// Encode 160 PCM samples into one 88-bit codeword. The encoder and decoder share the vocoder state, so a caller
// switches direction only after p25_imbe_reset() (half-duplex P25).
void p25_imbe_encode(const int16_t pcm[P25_IMBE_SAMPLES], uint8_t frame[P25_IMBE_BYTES]);
#endif
// Decode a fixed set of frames and log the per-frame cost (hal_micros), to measure the CK803S against the 20 ms
// speech period (docs/spec/p25.md 9).
void p25_imbe_bench(void);
#ifdef __cplusplus
}
#endif
#endif

#endif
