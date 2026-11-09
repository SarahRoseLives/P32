// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 The RadioFoundry Authors
// Ported from DM32-Multimode for P32 conventional P25 TX.
#ifndef P25_LIVE_FRAME_H
#define P25_LIVE_FRAME_H
#include <stdint.h>
#include <stdbool.h>
typedef struct {
    uint8_t hdu[99],ldu1[216],ldu2[216],tdu[18];
} p25_channel_frames;
// Precompute fixed clear-group control fields before starting audio/RF.
// Width validation only; callers apply channel policy separately.
bool p25_channel_build(p25_channel_frames *out,uint32_t nac,uint32_t tg,uint32_t source);
void p25_live_ldu_template(uint8_t out[216],const uint8_t payload[9][11],const uint8_t base[216]);
// Compatibility/default fixture: NAC 0x293, TG/source 1.
// Nine 88-bit payloads -> 216 bytes including FEC, whitening and status bits.
void p25_live_ldu(uint8_t out[216],const uint8_t payload[9][11],bool ldu2);
#endif
