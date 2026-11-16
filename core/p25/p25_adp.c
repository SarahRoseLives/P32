// SPDX-License-Identifier: MIT
// Copyright (c) 2026 The RadioFoundry Authors
#include "p25/p25_adp.h"

#include <string.h>

void p25_adp_rc4(const uint8_t *key, unsigned key_len, uint8_t *out, unsigned len, unsigned drop)
{
	uint8_t s[256];
	uint8_t i = 0, j = 0;

	for (unsigned k = 0; k < 256u; k++)
		s[k] = (uint8_t)k;
	for (unsigned k = 0; k < 256u; k++) {
		j = (uint8_t)(j + s[k] + (key_len ? key[k % key_len] : 0));
		uint8_t t = s[k]; s[k] = s[j]; s[j] = t;
	}
	i = 0;
	j = 0;
	for (unsigned n = 0; n < drop; n++) {
		i = (uint8_t)(i + 1u);
		j = (uint8_t)(j + s[i]);
		uint8_t t = s[i]; s[i] = s[j]; s[j] = t;
	}
	for (unsigned n = 0; n < len; n++) {
		i = (uint8_t)(i + 1u);
		j = (uint8_t)(j + s[i]);
		uint8_t t = s[i]; s[i] = s[j]; s[j] = t;
		out[n] = s[(uint8_t)(s[i] + s[j])];
	}
}

void p25_adp_keystream(const uint8_t key[P25_ADP_KEY_LEN], const uint8_t mi[9], uint8_t out[P25_ADP_STREAM_LEN])
{
	uint8_t material[P25_ADP_KEY_LEN + P25_ADP_MI_LEN];

	memcpy(material, key, P25_ADP_KEY_LEN);
	memcpy(material + P25_ADP_KEY_LEN, mi, P25_ADP_MI_LEN);
	p25_adp_rc4(material, sizeof material, out, P25_ADP_STREAM_LEN, 256);
	memset(material, 0, sizeof material);
}

void p25_adp_apply(uint8_t payload[11], const uint8_t stream[P25_ADP_STREAM_LEN], unsigned off, unsigned frame)
{
	unsigned at = off + 11u * frame;

	if (at + 11u > P25_ADP_STREAM_LEN)
		return;
	for (unsigned k = 0; k < 11u; k++)
		payload[k] ^= stream[at + k];
}
