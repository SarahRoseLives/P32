// SPDX-License-Identifier: MIT
// Copyright (c) 2026 The RadioFoundry Authors
// HR_C7000 P25 physical-layer transmit test engine (docs/spec/p25.md 10; modem-hrc7000.md 2.10, 9.5). The modem's
// PHY transmit mode sends the CPU's complete 288-bit blocks, one per 30 ms slot, with SEND_DATA0/1 alternating. This
// engine feeds the fixed P25 test stream from core/p25/tx/p25_test_tx.c and keys the board's existing DMR RF/PA hooks
// without starting the DMR engine. The test stream is a bench diagnostic: explicit ARM, explicit START, a 30 s ARM
// lifetime and a hard 12 s limit. Live voice uses the radio core's TX timeout instead.
//
// Interrupt and timer use (platform/hrc7000/platform.h):
//   PIC 29 (system)      PHY_READY (bit 12): one physical block boundary
//   PIC 30 / 31 (TS)     the other slot boundaries (duplicate PHY events are counted and ignored)
//   PIC 32 (RF TX)       RF chip to TX for the first block; the PA one-shot follows
//   Timer 3              PA one-shot (the DMR PA timer, documented as such; the DMR engine is off here)
//   Timer 6              test-stream hard stop, armed at START (live voice is stopped by the radio core's TX timeout)
#include "config.h"

#if CONFIG_P25_TX

#include <stddef.h>
#include <string.h>
#include "p25_tx.h"
#include "platform.h"
#include "board.h"
#include "dmr_glue.h"
#include "p25_probe.h"
#include "p25/tx/p25_test_tx.h"
#include "p25/tx/p25_live_tx.h"
#include "FreeRTOS.h"
#include "task.h"

#define P25_TX_PHY_READY	(1u << 12)
#define P25_TX_ARM_TTL_US	30000000u
#define P25_TX_HARD_TIMER	6
#define P25_TX_WORKER_STACK	384	// words: a poll, the RF chip restore and the board's bit-banged bus
#define P25_TX_WORKER_PRIORITY	(configMAX_PRIORITIES - 2)

static hrc7000_t *dev;
static const hrc_dmr_board_t *hooks;
static p25_test_tx tx;
static p25_live_tx live_tx;
static p25_test_tx raw_timing;
static const uint8_t *raw_data;
static uint32_t raw_len, raw_pos;
static bool raw_mode;
static bool live_mode;
static uint32_t live_ldus, live_underruns;
static bool live_frames_set;
static volatile bool hardware_running, pa_started;
static bool rf_prepared;
static bool cleanup_needed;
static bool finishing;
static bool armed, allow_rf_granted, arm_expired, setup_failed;
static bool cal_ok;
static uint32_t frequency, arm_until;
static uint32_t sys_irqs, ts_irqs, rftx_irqs;
static uint32_t saved_rf_mode;
static uint8_t tx_variant, half;
static bool invert;
static bool worker_started;

static void p25_tx_worker(void *arg);

static p25_test_tx *timing(void)
{
	if (raw_mode) {
		return &raw_timing;
	}
	return live_mode ? &live_tx.timing : &tx;
}

static bool engine_active(void)
{
	return hardware_running || p25_test_tx_active(&tx) || p25_test_tx_active(&live_tx.timing) ||
	       p25_test_tx_active(&raw_timing);
}

static void wr(uint32_t off, uint32_t value)
{
	hrc7000_wr(dev, off, value);
}

// The board attached the modem and its RF/PA hooks in radio init (hrc_dmr_board_init); borrow them without starting
// the DMR engine.
static void bind(void)
{
	dev = hrc_dmr_modem();
	hooks = hrc_dmr_board_hooks();
}

static bool available(void)
{
	return dev != NULL && hooks != NULL && hooks->rf_switch != NULL && hooks->pa_on != NULL &&
	       hooks->p25_prepare != NULL && hooks->p25_finish != NULL && hooks->p25_ready != NULL &&
	       board.radio != NULL && board.radio->audio != NULL;
}

static bool battery_ok(void)
{
	if (!board_has_battery() || board.battery_critical_mv == 0 || board.power == NULL ||
	    board.power->battery_mv == NULL) {
		return true;
	}
	return board.power->battery_mv() >= board.battery_critical_mv;
}

// Called with CPU interrupts masked, including from the hard-stop ISR. RF off first, then everything else the
// engine borrowed; task context finishes the board's RF chip and the modem's RF_MODE in p25_finish.
static void hardware_stop(void)
{
	if (dev != NULL) {
		wr(C7_LAYER2_TXRX_CTRL, 0);
		wr(C7_LAYER2_CONTROL, 0xc0);
		wr(C7_SYS_INTERP_MASK, 0);
	}
	hrc_irq_enable(IRQ_MODEM_SYS, false);
	hrc_irq_enable(IRQ_MODEM_TS_TX, false);
	hrc_irq_enable(IRQ_MODEM_TS_RX, false);
	hrc_irq_enable(IRQ_MODEM_RF_TX, false);
	hrc_irq_enable(IRQ_MODEM_RF_RX, false);
	hrc_timer_stop(TIMER_PA);
	hrc_timer_stop(P25_TX_HARD_TIMER);
	if (hardware_running && hooks != NULL) {
		hooks->rf_switch(false);
	}
	board_rf_safe();
	hardware_running = false;
	pa_started = false;
}

// Task context: put the modem's and the RF chip's mode back after any transmission or failed setup.
static void finish(void)
{
	uint32_t saved = hrc_irq_save();
	if (finishing) {
		hrc_irq_restore(saved);
		return;	// the worker and STOP may race; one restore is enough
	}
	finishing = true;
	hardware_stop();
	if (dev != NULL && rf_prepared) {
		wr(C7_RF_MODE, saved_rf_mode);
	}
	rf_prepared = false;
	hrc_irq_restore(saved);
	if (hooks != NULL && hooks->p25_finish != NULL) {
		hooks->p25_finish();
	}
	hrc_dmr_irqs_reinstall();	// the DMR engine (and the FM receive audio hook) get their handlers back
	saved = hrc_irq_save();
	cleanup_needed = false;
	finishing = false;
	hrc_irq_restore(saved);
}

static void boundary(void)
{
	uint8_t block[P25_TX_BLOCK_BYTES];

	if (!hardware_running) {
		return;
	}
	uint32_t now = hrc_micros();
	if (raw_mode) {
		if (raw_timing.state != P25_TX_WAITING && raw_timing.state != P25_TX_STREAMING) {
			return;
		}
		if (raw_pos >= raw_len) {
			if (raw_timing.state == P25_TX_STREAMING) {
				raw_timing.state = P25_TX_DRAINING;
				raw_timing.drain_until_us = now + P25_TX_DRAIN_US;
			}
			return;
		}
		raw_timing.state = P25_TX_STREAMING;
		raw_timing.last_boundary_us = now;
		raw_timing.have_boundary = true;
		for (uint32_t i = 0; i < P25_TX_BLOCK_BYTES; i++) {
			block[i] = raw_pos < raw_len ? raw_data[raw_pos] : 0x77;
			if (raw_pos < raw_len) {
				raw_pos++;
			}
		}
		raw_timing.offset += P25_TX_BLOCK_BYTES;
		raw_timing.blocks++;
		if (raw_pos >= raw_len) {
			raw_timing.state = P25_TX_DRAINING;
			raw_timing.drain_until_us = now + P25_TX_DRAIN_US;
		}
	} else {
	p25_test_tx *t = timing();
	bool ok = live_mode ? p25_live_tx_boundary(&live_tx, now, block) : p25_test_tx_boundary(&tx, now, block);
	if (!ok) {
		if (!p25_test_tx_active(t)) {
			hardware_stop();
		} else if (t->state == P25_TX_DRAINING && now - t->last_boundary_us >= 15000u) {
			wr(C7_LAYER2_TXRX_CTRL, 0);
		}
		// A duplicate SYS/TS event must not clear a pending send.
		return;
	}
	}
	// SEND_DATA0/1 alternate. Updating both replaces the half that is still playing on this chip.
	uint8_t policy = tx_variant & 0x0cu;
	for (uint32_t i = 0; i < P25_TX_BLOCK_BYTES; i++) {
		if (policy == 0 || half == 0) {
			dev->buf[i] = block[i];
		}
		if (policy == 0 || half == 1) {
			dev->buf[0x24u + i] = block[i];
		}
	}
	half ^= 1u;
	wr(C7_LAYER2_TXRX_CTRL, 0x80u | ((tx_variant & 2u) ? 1u : 0u));
}

static void isr_sys(void)
{
	uint32_t list = hrc7000_rd(dev, C7_SYS_INTERP_LIST);

	wr(C7_SYS_INTERP_CLEAR, list);
	sys_irqs++;
	if (list & P25_TX_PHY_READY) {
		boundary();
	}
}

static void isr_slot(void)
{
	wr(C7_INTERP_CLEAR, C7_INT_TS_TX | C7_INT_TS_RX);
	ts_irqs++;
	boundary();
}

static void isr_rftx(void)
{
	wr(C7_INTERP_CLEAR, C7_INT_RF_TX);
	rftx_irqs++;
	if (hardware_running && p25_test_tx_active(timing()) && !pa_started) {
		pa_started = true;
		hooks->rf_switch(true);
		hrc_timer_oneshot(TIMER_PA, hooks->pa_delay_us);
	}
}

static void isr_rfrx(void)
{
	wr(C7_INTERP_CLEAR, C7_INT_RF_RX);
}

static void isr_pa(void)
{
	hrc_timer_ack(TIMER_PA);
	hrc_timer_stop(TIMER_PA);
	if (hardware_running && p25_test_tx_active(timing())) {
		hooks->pa_on();
	}
}

static void isr_hard_stop(void)
{
	hrc_timer_ack(P25_TX_HARD_TIMER);
	p25_test_tx_abort(&tx, P25_TX_TIMEOUT);
	hardware_stop();
}

// One table for the engine; the modem sources stay masked until start enables them. Timers 3 and 6 are the PA
// one-shot and the hard stop, and stay enabled (stopped timers can't raise).
static void install_irqs(void)
{
	hrc_irq_register(IRQ_MODEM_SYS, isr_sys);
	hrc_irq_register(IRQ_MODEM_TS_TX, isr_slot);
	hrc_irq_register(IRQ_MODEM_TS_RX, isr_slot);
	hrc_irq_register(IRQ_MODEM_RF_TX, isr_rftx);
	hrc_irq_register(IRQ_MODEM_RF_RX, isr_rfrx);
	hrc_irq_register(IRQ_TIMER3, isr_pa);
	hrc_irq_register(IRQ_TIMER6, isr_hard_stop);
	hrc_irq_enable(IRQ_TIMER3, true);
	hrc_irq_enable(IRQ_TIMER6, true);
}

static bool arm_common(uint32_t hz, uint8_t variant, bool polarity, bool allow_rf, bool live)
{
	bind();
	if (!allow_rf || !available() || variant > 11u || !board_in_band(hz, true) || !hooks->p25_ready(hz) ||
	    !battery_ok() || hrc_dmr_active() || hrc_p25_probe_active()) {
		return false;
	}
	cal_ok = true;
	uint32_t saved = hrc_irq_save();
	bool busy = armed || cleanup_needed || finishing || engine_active();
	hrc_irq_restore(saved);
	if (busy) {
		return false;
	}
	if (!worker_started) {
		if (xTaskCreate(p25_tx_worker, "p25tx", P25_TX_WORKER_STACK, NULL, P25_TX_WORKER_PRIORITY, NULL) != pdPASS) {
			return false;
		}
		worker_started = true;
	}
	live_mode = live;
	if (live) {
		p25_live_tx_init(&live_tx);
		live_ldus = live_underruns = 0;
		live_frames_set = false;
	} else {
		p25_test_tx_init(&tx);
	}
	frequency = hz;
	tx_variant = variant;
	invert = polarity;
	arm_until = hrc_micros() + P25_TX_ARM_TTL_US;
	allow_rf_granted = true;
	armed = true;
	arm_expired = false;
	setup_failed = false;
	return true;
}

static bool arm(uint32_t hz, uint8_t variant, bool polarity, bool allow_rf)
{
	return arm_common(hz, variant, polarity, allow_rf, false);
}

static bool arm_live(uint32_t hz, uint8_t variant, bool polarity, bool allow_rf)
{
	if (!arm_common(hz, variant, polarity, allow_rf, true)) {
		return false;
	}
	// Prepare the RF chip, PA drive and microphone path now, so the first microphone blocks are real audio. The PHY
	// stream and PA stay off until start_live() has two complete LDUs.
	bind();
	board.radio->audio(false);
	if (!hooks->p25_prepare(frequency)) {
		armed = false;
		allow_rf_granted = false;
		setup_failed = true;
		return false;
	}
	rf_prepared = true;
	return true;
}

static bool set_frames(const uint8_t hdu[99], const uint8_t tdu[18])
{
	if (!live_mode || !armed || hardware_running || p25_test_tx_active(&live_tx.timing)) {
		return false;
	}
	memcpy(live_tx.hdu, hdu, sizeof live_tx.hdu);
	memcpy(live_tx.tdu, tdu, sizeof live_tx.tdu);
	live_frames_set = true;
	return true;
}

static bool push_ldu(const uint8_t ldu[216])
{
	if (!live_mode || !live_frames_set) {
		return false;
	}
	uint32_t saved = hrc_irq_save();
	bool ok = p25_live_tx_push(&live_tx, ldu);
	hrc_irq_restore(saved);
	if (ok) {
		live_ldus++;
	} else {
		live_underruns++;
	}
	return ok;
}

static bool finish_live(void)
{
	if (!live_mode) {
		return false;
	}
	uint32_t saved = hrc_irq_save();
	bool ok = p25_live_tx_finish(&live_tx);
	hrc_irq_restore(saved);
	return ok;
}

static bool start_engine(bool live, bool raw, uint32_t now_us)
{
	live_mode = live;
	raw_mode = raw;
	if (!armed || !allow_rf_granted) {
		return false;
	}
	if ((int32_t)(now_us - arm_until) >= 0) {
		armed = false;
		allow_rf_granted = false;
		arm_expired = true;
		return false;
	}
	bind();
	if (!available() || !board_in_band(frequency, true) || !hooks->p25_ready(frequency) || !battery_ok() ||
	    hrc_dmr_active() || hrc_p25_probe_active()) {
		armed = false;
		allow_rf_granted = false;
		setup_failed = true;
		return false;
	}
	if (live && !live_frames_set) {
		return false;
	}
	if (raw && (raw_data == NULL || raw_len == 0u)) {
		return false;
	}
	cal_ok = true;
	uint32_t saved = hrc_irq_save();
	if (finishing || hardware_running || p25_test_tx_active(timing())) {
		hrc_irq_restore(saved);
		return false;
	}
	hrc_irq_restore(saved);

	// One ARM grants one START, even if the setup below fails.
	armed = false;
	allow_rf_granted = false;
	if (!live) {
		board.radio->audio(false);
	}
	if (!rf_prepared && !hooks->p25_prepare(frequency)) {
		setup_failed = true;
		return false;
	}
	rf_prepared = true;
	cleanup_needed = true;

	saved = hrc_irq_save();
	if (!live) {
		hrc7000_audio_bits(dev, 0, C7_AUDIO_RING_PLAY | C7_AUDIO_FM_PLAY | C7_AUDIO_PCM_SPK | C7_AUDIO_PCM_MIC);
	}
	saved_rf_mode = hrc7000_rd(dev, C7_RF_MODE);
	wr(C7_RF_MODE, saved_rf_mode ^ (invert ? (1u << 7) : 0u));
	wr(C7_LAYER2_TXRX_CTRL, 0);
	wr(C7_LAYER2_SLOTON, 0);
	wr(C7_WORK_MODE, C7_WORK_MODE_FM);
	wr(C7_WORK_MODE, (tx_variant & 1u) ? 0x02u : C7_WORK_MODE_PHY);
	if (!live) {
		wr(C7_AUDIO_BUFFER_CLR, 3);
	}
	for (uint32_t i = 0; i < 72; i++) {
		dev->buf[i] = 0x77;
	}
	half = (tx_variant & 0x0cu) == 8u ? 1u : 0u;
	sys_irqs = ts_irqs = rftx_irqs = 0;
	wr(C7_SYS_INTERP_CLEAR, 0xffffffffu);
	wr(C7_INTERP_CLEAR, C7_INT_TS_TX | C7_INT_TS_RX | C7_INT_RF_TX | C7_INT_RF_RX);
	wr(C7_SYS_INTERP_MASK, P25_TX_PHY_READY);
	bool stream_started;
	if (raw) {
		raw_pos = 0;
		stream_started = p25_test_tx_start(&raw_timing, hrc_micros(), true);
	} else {
		stream_started = live ? p25_live_tx_start(&live_tx, hrc_micros()) :
			p25_test_tx_start(&tx, hrc_micros(), true);
	}
	if (!stream_started) {
		hrc_irq_restore(saved);
		setup_failed = true;
		finish();
		return false;
	}
	hardware_running = true;
	pa_started = false;
	install_irqs();
	hrc_irq_enable(IRQ_MODEM_SYS, true);
	hrc_irq_enable(IRQ_MODEM_TS_TX, true);
	hrc_irq_enable(IRQ_MODEM_TS_RX, true);
	hrc_irq_enable(IRQ_MODEM_RF_TX, true);
	hrc_irq_enable(IRQ_MODEM_RF_RX, true);
	if (!live) {
		hrc_timer_oneshot(P25_TX_HARD_TIMER, P25_TX_MAX_US);
	}
	wr(C7_LAYER2_CONTROL, 0x82u);	// Own slot clock; the CPU supplies complete PHY blocks.
	hrc_irq_restore(saved);
	// Key the RF path at START. The per-burst RF TX interrupt keys the first arm, but after the DMR IRQs are
	// reinstalled by a previous finish it does not fire on the next arm; the transmitter would show TX with no RF.
	if (!pa_started) {
		pa_started = true;
		hooks->rf_switch(true);
		hrc_timer_oneshot(TIMER_PA, hooks->pa_delay_us);
	}
	return true;
}

static bool start(uint32_t now_us)
{
	return start_engine(false, false, now_us);
}

static bool start_live(uint32_t now_us)
{
	return start_engine(true, false, now_us);
}

static bool start_raw(const uint8_t *data, uint32_t len, uint32_t now_us)
{
	raw_data = data;
	raw_len = len;
	return start_engine(false, true, now_us);
}

static void stop(void)
{
	armed = false;
	allow_rf_granted = false;
	uint32_t saved = hrc_irq_save();
	bool cleanup = cleanup_needed || hardware_running || rf_prepared || engine_active();
	p25_test_tx_abort(&tx, P25_TX_CANCELLED);
	p25_test_tx_abort(&live_tx.timing, P25_TX_CANCELLED);
	p25_test_tx_abort(&raw_timing, P25_TX_CANCELLED);
	hrc_irq_restore(saved);
	if (cleanup) {
		finish();
	}
}

static void disarm(void)
{
	arm_expired = false;
	setup_failed = false;
	stop();
}

static void poll(uint32_t now_us, bool ptt_down)
{
	if (armed && (int32_t)(now_us - arm_until) >= 0) {
		armed = false;
		allow_rf_granted = false;
		arm_expired = true;
	}
	bool cleanup = cleanup_needed;
	if (!cleanup && !engine_active()) {
		return;
	}
	uint32_t saved = hrc_irq_save();
	p25_test_tx *t = timing();
	if (live_mode) {
		p25_test_tx_poll_limit(t, now_us, ptt_down, LIVE_RF_MAX_US);
	} else {
		p25_test_tx_poll(t, now_us, ptt_down);
	}
	bool active = p25_test_tx_active(t);
	if (!active && hardware_running) {
		hardware_stop();
	}
	hrc_irq_restore(saved);
	if (cleanup && !active) {
		finish();
	}
}

static bool active(void)
{
	uint32_t saved = hrc_irq_save();
	bool running = engine_active();
	hrc_irq_restore(saved);
	return running;
}

static void stats(hal_p25_tx_stats_t *out)
{
	uint32_t saved = hrc_irq_save();
	out->armed = armed;
	out->hardware_running = hardware_running;
	p25_test_tx *t = timing();
	out->live = live_mode || raw_mode;
	out->ldus = live_ldus;
	out->underruns = live_underruns;
	out->state = (uint8_t)t->state;
	out->reason = (uint8_t)t->reason;
	out->variant = tx_variant;
	out->invert = invert;
	out->arm_expired = arm_expired;
	out->setup_failed = setup_failed;
	out->calibration_valid = cal_ok;
	out->frequency_hz = frequency;
	if (armed) {
		int32_t left = (int32_t)(arm_until - hrc_micros());
		out->arm_remaining_s = left > 0 ? ((uint32_t)left + 999999u) / 1000000u : 0;
	} else {
		out->arm_remaining_s = 0;
	}
	out->stream_bytes = raw_mode ? raw_len : (live_mode ? 0 : p25_test_tx_stream_bytes());
	out->offset = t->offset;
	out->blocks = t->blocks;
	out->duplicate_irqs = t->duplicate_irqs;
	out->late_irqs = t->late_irqs;
	out->max_gap_us = t->max_gap_us;
	out->sys_irqs = sys_irqs;
	out->ts_irqs = ts_irqs;
	out->rftx_irqs = rftx_irqs;
	hrc_irq_restore(saved);
}

static void p25_tx_worker(void *arg)
{
	(void)arg;
	for (;;) {
		poll(hrc_micros(), true);	// the fixed test stream has no PTT; STOP still aborts at once
		ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(10));
	}
}

const hal_p25_tx_t hrc_p25_tx = {
	.arm = arm,
	.arm_live = arm_live,
	.set_frames = set_frames,
	.push_ldu = push_ldu,
	.finish = finish_live,
	.disarm = disarm,
	.start = start,
	.start_live = start_live,
	.start_raw = start_raw,
	.stop = stop,
	.poll = poll,
	.active = active,
	.stats = stats,
};

#endif	// CONFIG_P25_TX
