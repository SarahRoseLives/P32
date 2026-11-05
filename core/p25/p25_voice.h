// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 The RadioFoundry Authors
// Conventional P25 Phase 1 voice receive: the 8 kHz FM discriminator through a software demodulator, LDU FEC and the
// fixed-point IMBE decoder to the speaker (docs/spec/p25.md 6, 9). The caller tunes the radio and leaves it idle; the
// engine routes the discriminator, starts the PCM stream and decodes in its own task. RX only.
#ifndef RFOUNDRY_P25_VOICE_H
#define RFOUNDRY_P25_VOICE_H

#include <stdbool.h>
#include <stdint.h>
#include "config.h"

#if CONFIG_P25_VOICE
#ifdef __cplusplus
extern "C" {
#endif
// Start and stop (task level). Start needs the FM discriminator stream and the PCM audio path.
bool p25_voice_start(void);
void p25_voice_stop(void);
bool p25_voice_running(void);

typedef struct {
	uint32_t samples;	// 8 kHz discriminator samples pushed through the demodulator
	uint32_t sync_tests;	// demodulator sync correlations tried
	uint32_t acquisitions;	// synced preambles
	uint32_t units;		// complete validated frames
	uint32_t voices;	// decoded voice payloads
	uint32_t corrected;	// Golay/Hamming corrections summed
	uint32_t underruns;	// audio blocks with no queued frame
	uint32_t overflows;	// voice frames dropped because the PCM ring was full
} p25_voice_stats_t;
void p25_voice_stats(p25_voice_stats_t *out);
#ifdef __cplusplus
}
#endif
#endif

#endif
