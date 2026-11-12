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
// Start locked to a grant's NAC and talkgroup (0 = any) so a following receiver ignores other calls
bool p25_voice_start_filtered(uint16_t nac, uint16_t tg);
// True once the call ended (TDU/TDULC) or went quiet; the caller stops the receiver and returns to the control channel
bool p25_voice_ended(void);
// Level processing (DC blocker, AGC, noise gate): off plays the codec output as-is (reference behavior)
void p25_voice_set_agc(bool on);
bool p25_voice_agc(void);
void p25_voice_stop(void);
bool p25_voice_running(void);

typedef struct {
	uint32_t samples;	// 8 kHz discriminator samples pushed through the demodulator
	uint32_t sync_tests;	// demodulator sync correlations tried
	uint32_t acquisitions;	// synced preambles
	uint32_t accepted, repeated, muted;	// voice-policy actions
	uint16_t peak;		// largest |sample| of an accepted frame
	uint32_t max_push_us, max_frame_us;	// worst demodulator sample and codec frame (diagnostics)
	uint32_t resets;	// codec resets (new calls / metadata changes)
	uint32_t drops;	// discriminator halves dropped because the decoder fell behind
	uint32_t units;		// complete validated frames
	uint32_t voices;	// decoded voice payloads
	uint32_t corrected;	// Golay/Hamming corrections summed
	uint16_t nac, group;	// the call's NAC and talkgroup (when known)
	uint32_t source;	// the call's source unit (when known)
	uint8_t state;		// p25_receive_class: 0 wait, 1 clear, 2 encrypted, 3 unsupported, 4 bad
	uint32_t underruns;	// audio blocks with no queued frame
	uint32_t overflows;	// voice frames dropped because the PCM ring was full
} p25_voice_stats_t;
void p25_voice_stats(p25_voice_stats_t *out);
#ifdef __cplusplus
}
#endif
#endif

#endif
