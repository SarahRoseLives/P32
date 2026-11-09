// SPDX-License-Identifier: MIT
// Copyright (c) 2026 The RadioFoundry Authors
// HR_C7000 DMR modem glue shared by the boards (dmr_glue.c): the slot engine on the SoC's modem interrupts, the PA
// one-shot, the deferred event task and the hal_dmr_modem_t over them. A board supplies what differs: how its RF chip
// switches between receive and transmit for each burst and how its PA is keyed
#ifndef HRC_DMR_GLUE_H
#define HRC_DMR_GLUE_H

#include <stdint.h>
#include <stdbool.h>
#include "config.h"
#include "hal_dmr_modem.h"
#include "modem/hrc7000/hrc7000.h"

typedef struct {
	// From the modem interrupts (ISR context, interrupts off): the RF chip to TX for our burst or back to RX after it.
	// After a burst the board also drops the PA drive
	void (*rf_switch)(bool tx);
	// The PA one-shot expired (ISR context): key the PA if the transmission is still on
	void (*pa_on)(void);
	// Microseconds from the start of a burst to the PA one-shot (a stock "DMR PA" setting, default 28 ms)
	uint32_t pa_delay_us;
#if CONFIG_P25_TX
	// P25 PHY transmit test (platform/hrc7000/p25_tx.c): put the RF chip in its wide data path on hz without starting
	// the DMR engine. The modem itself is configured by the platform. Task context, before the PHY interrupts; false
	// when the board can't. The per-burst rf_switch/pa_on hooks above key the PA.
	bool (*p25_prepare)(uint32_t hz);
	// The P25 transmission has ended: RF chip back to FM receive, PA off. Task context.
	void (*p25_finish)(void);
	// Whether the board's calibration and RF path allow a PHY transmission on hz (the platform also checks the band)
	bool (*p25_ready)(uint32_t hz);
#endif
} hrc_dmr_board_t;

#if CONFIG_HRC_MODEM
// Once, from the radio init, after the modem is attached: interrupts, PA timer, task
void hrc_dmr_board_init(hrc7000_t *modem, const hrc_dmr_board_t *hooks);
void hrc_dmr_enter(void);		// the modem has just entered DMR: engine on, interrupts on
void hrc_dmr_leave(void);
void hrc_dmr_rx_restart(void);
bool hrc_dmr_tx_active(void);
void hrc_dmr_tx_drop(void);
#if CONFIG_P25_PROBE || CONFIG_P25 || CONFIG_P25_VOICE || CONFIG_P25_TX
// A hook on every system interrupt's bit list (the P25 probe, docs/spec/p25.md 5.1). Interrupt context
void hrc_dmr_sys_hook(void (*fn)(uint32_t list));
// Whether the DMR engine is running (the probe stops it before taking the modem)
bool hrc_dmr_active(void);
#endif
#if CONFIG_P25_TX
// The P25 PHY transmit test borrows the modem and the board's DMR RF/PA hooks without running the DMR engine. The
// board attached both in hrc_dmr_board_init (radio init); NULL before that.
hrc7000_t *hrc_dmr_modem(void);
const hrc_dmr_board_t *hrc_dmr_board_hooks(void);
// Put the DMR engine's interrupt handlers back after the P25 test borrowed them (task context)
void hrc_dmr_irqs_reinstall(void);
#endif
// FM receive audio (fm_rx_audio.c): fn runs from the modem's system interrupt for each half the modem fills; NULL
// stops it. The system interrupt stays on for it outside DMR
bool hrc_dmr_fm_rx_hook(void (*fn)(void));

extern const hal_dmr_modem_t hrc_dmr;

// Bring-up counters: lines of at most about 20 characters, and the reset
#if defined(CONFIG_HOTSPOT_SPLIT) && CONFIG_HOTSPOT_SPLIT
#define HRC_DMR_DIAG_LINES	14	// and the split duplex TX half's BS outbound (two "bs" lines)
#else
#define HRC_DMR_DIAG_LINES	12
#endif
bool hrc_dmr_stats_line(uint8_t i, char *buf, uint8_t len);
void hrc_dmr_stats_reset(void);
#else
static inline void hrc_dmr_board_init(hrc7000_t *modem, const hrc_dmr_board_t *hooks) { (void)modem; (void)hooks; }
static inline void hrc_dmr_enter(void) {}
static inline void hrc_dmr_leave(void) {}
static inline void hrc_dmr_rx_restart(void) {}
static inline bool hrc_dmr_tx_active(void) { return false; }
static inline void hrc_dmr_tx_drop(void) {}
static inline bool hrc_dmr_fm_rx_hook(void (*fn)(void)) { (void)fn; return false; }
#endif

#endif
