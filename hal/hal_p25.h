// SPDX-License-Identifier: MIT
// Copyright (c) 2026 The RadioFoundry Authors
// The P25 physical-layer receive (docs/spec/p25.md 5.1, 8): a platform whose demodulator has a physical-layer receive
// test mode puts the P25 frame sync there and hands back the receive RAM. The debug link's bench probe starts and
// stops it and logs the raw block; the control-channel monitor reads the decoded control channel through last_cc.
// Optional: board.p25_probe is NULL where there is none.
#ifndef HAL_P25_H
#define HAL_P25_H

#include <stdint.h>
#include <stdbool.h>

// The control-channel values a decoded TSBK carries, for the monitor screen (docs/spec/p25.md 8). A field keeps its
// last value when a later TSBK's opcode does not set it. valid: a control channel has been decoded since start.
typedef struct {
	bool valid;
	uint8_t opcode;		// the last TSBK's opcode
	uint16_t nac;		// from the NID
	uint8_t duid;		// from the NID
	uint32_t wacn;
	uint16_t sys;
	uint8_t rfss;
	uint8_t site;
	uint16_t tg;
	uint16_t vch;
	uint32_t freq_hz;	// from an IdenUp
	uint32_t blocks;	// PHY blocks captured since start
	uint32_t voice_hz;	// the last grant's voice channel (band plan applied; 0 unknown)
	uint32_t grants;	// grants seen since start (a change means a new grant)
} hal_p25_cc_t;

typedef struct {
	// Take the demodulator for the probe. False when it can't (no modem, transmitting). The radio stays on its tuned
	// frequency: the caller sets that first. The platform saves the registers it changes and restores them on stop.
	bool (*start)(void);
	// Optional (NULL: none): start with a variant byte for bench work, bit 0 = continuous (1) vs timeslot (0),
	// bit 1 = keep RF_MODE bit 11 (dual PHY). start() is the default variant.
	bool (*start_ex)(uint8_t variant);
	void (*stop)(void);
	// The last captured block: its frame-type and physical-layer state words, the system interrupt list that carried
	// it, how many blocks have arrived, and the raw receive-RAM bytes. Returns the bytes written to data (0: nothing
	// captured yet).
	uint8_t (*read)(uint32_t *type_info, uint32_t *phy_state, uint32_t *interrupt_list, uint32_t *blocks,
			uint8_t *data, uint8_t max);

	// Optional (NULL: none): the 8 kHz FM discriminator, for a software P25 demodulator. The radio must be tuned in FM
	// and idle; start routes the RF chip's audio to the modem, stop gives it back. read returns up to max samples.
	bool (*audio_start)(void);
	void (*audio_stop)(void);
	uint16_t (*audio_read)(int16_t *buf, uint16_t max);

	// Optional (NULL: none): the last decoded control channel, for the monitor screen. Runs the decode in the
	// caller's task when new blocks have arrived, so it must be called from task context (the radio task's poll).
	// Returns false until a control channel has been decoded since start.
	bool (*last_cc)(hal_p25_cc_t *out);
} hal_p25_probe_t;

#endif
