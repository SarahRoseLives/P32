// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 The RadioFoundry Authors
// Fixed-storage wrapper around the fixed-point P25 IMBE vocoder (Pavel Yazev, GPL-3.0-or-later): no heap and no static
// constructors, so it links into the freestanding csky-elf image (docs/spec/p25.md 6). The codec state is one static
// imbe_vocoder_impl, constructed in place on the first decode (5,736 bytes).
#include "config.h"
#if CONFIG_P25_VOICE

#include <stddef.h>
#include <stdint.h>
#include "imbe_vocoder_impl.h"
#include "p25/p25_imbe.h"

alignas(imbe_vocoder_impl) static unsigned char state[sizeof(imbe_vocoder_impl)];
inline void *operator new(size_t, void *p) noexcept { return p; }
static imbe_vocoder_impl *voc;

static void codec_construct(void)
{
	voc = new (state) imbe_vocoder_impl();
}

void p25_imbe_reset(void)
{
	if (voc) {
		voc->~imbe_vocoder_impl();
	}
	codec_construct();
}

void p25_imbe_decode(const uint8_t frame[P25_IMBE_BYTES], int16_t pcm[P25_IMBE_SAMPLES])
{
	uint8_t copy[P25_IMBE_BYTES];

	if (!voc) {
		codec_construct();
	}
	for (int i = 0; i < P25_IMBE_BYTES; i++) {
		copy[i] = frame[i];
	}
	voc->decode_4400(pcm, copy);
}

#if CONFIG_P25_TX
void p25_imbe_encode(const int16_t pcm[P25_IMBE_SAMPLES], uint8_t frame[P25_IMBE_BYTES])
{
	int16_t samples[P25_IMBE_SAMPLES];

	if (!voc) {
		codec_construct();
	}
	for (int i = 0; i < P25_IMBE_SAMPLES; i++) {
		samples[i] = pcm[i];
	}
	voc->encode_4400(samples, frame);
}
#endif

#endif
