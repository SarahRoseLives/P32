// SPDX-License-Identifier: MIT
// Copyright (c) 2026 The RadioFoundry Authors
// The P25 physical-layer receive bench probe (docs/spec/p25.md 5.1). A platform whose demodulator has a physical-layer
// receive test mode can put it there and hand back the last block; the debug link starts and stops it and logs the
// block. Optional: board.p25_probe is NULL where there is none.
#ifndef HAL_P25_H
#define HAL_P25_H

#include <stdint.h>
#include <stdbool.h>

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
} hal_p25_probe_t;

#endif
