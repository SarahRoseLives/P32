// SPDX-License-Identifier: MIT
// Copyright (c) 2026 The RadioFoundry Authors
// P25 Phase 1 physical-layer transmit test engine (docs/spec/p25.md 10): a bounded, explicitly armed transmission of
// the fixed P25 test stream through the modem's PHY transmitter. Optional: board.p25_tx is NULL where there is none.
// The core only arms, starts, stops and reads counters here; registers stay in the platform.
#ifndef HAL_P25_TX_H
#define HAL_P25_TX_H

#include <stdint.h>
#include <stdbool.h>

typedef struct {
	bool armed;			// an ARM is outstanding (one explicit START may use it)
	bool hardware_running;		// the modem and PA are on
	uint8_t state;			// p25_tx_state from core/p25/tx/p25_test_tx.h
	uint8_t reason;			// p25_tx_reason from the same header
	uint8_t variant;		// the armed variant, 0..11
	bool invert;			// the armed RF polarity
	bool arm_expired;		// the last ARM ran out before START
	bool setup_failed;		// the last START could not set up the radio
	bool calibration_valid;		// the board's factory calibration is usable
	uint32_t frequency_hz;		// the armed frequency
	uint32_t arm_remaining_s;	// seconds left in the ARM, 0 when none
	uint32_t stream_bytes;		// the fixed stream's length (test mode) or 0 (live)
	uint32_t offset;		// bytes the stream has handed to the modem
	uint32_t ldus;			// live: complete LDUs handed to the engine
	uint32_t underruns;		// live: LDU pushes refused because the queue was full/aborted
	bool live;			// the engine is in live-voice mode
	uint32_t blocks;		// physical 36-byte blocks sent
	uint32_t duplicate_irqs;	// boundary interrupts too close together
	uint32_t late_irqs;		// boundary gaps past the allowed maximum
	uint32_t max_gap_us;		// longest boundary gap seen
	uint32_t sys_irqs;		// modem system interrupts serviced
	uint32_t ts_irqs;		// timeslot interrupts serviced
	uint32_t rftx_irqs;		// RF TX interrupts serviced
} hal_p25_tx_stats_t;

typedef struct {
	// Grant one explicit transmission at hz. allow_rf must be true; without it the call is refused. Refused too while
	// another transmission is active, when the radio is out of band, uncalibrated, on a critical battery, or when the
	// DMR engine or the P25 receive probe owns the modem.
	bool (*arm)(uint32_t hz, uint8_t variant, bool invert, bool allow_rf);
	// Drop an outstanding ARM. A running transmission is stopped too.
	void (*disarm)(void);
	// Begin the armed test transmission. False when it was not armed, the ARM ran out, or the radio could not be set up.
	bool (*start)(uint32_t now_us);
	// Live voice: arm the same PHY path, supply the call's HDU/TDU templates and feed complete 216-byte LDUs. RF does
	// not start until start_live() and at least two LDUs are queued. finish() ends the call after the last LDU.
	bool (*arm_live)(uint32_t hz, uint8_t variant, bool invert, bool allow_rf);
	bool (*set_frames)(const uint8_t hdu[99], const uint8_t tdu[18]);
	bool (*push_ldu)(const uint8_t ldu[216]);
	bool (*finish)(void);
	bool (*start_live)(uint32_t now_us);
	// Raw PHY stream: already-packed on-air bytes (four dibits per byte, MSB first) for a control-channel burst. The
	// caller keeps the buffer valid until active() is false. Starts only when armed; the stream ends with the buffer.
	bool (*start_raw)(const uint8_t *data, uint32_t len, uint32_t now_us);
	// Stop now: RF off, PA off, modem and RF chip restored, RF-safe.
	void (*stop)(void);
	// Service the engine: finishes the drain, aborts on PTT release, enforces the time limit. Task context.
	void (*poll)(uint32_t now_us, bool ptt_down);
	// True while a transmission is running or draining.
	bool (*active)(void);
	// A snapshot of the engine's counters (interrupt-safe).
	void (*stats)(hal_p25_tx_stats_t *out);
} hal_p25_tx_t;

#endif
