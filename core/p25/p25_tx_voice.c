// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 The RadioFoundry Authors
// Conventional P25 Phase 1 voice transmit (docs/spec/p25.md 10): 20 ms microphone blocks through the fixed-point IMBE
// encoder, packed into nine-frame LDUs and handed to the platform's P25 PHY transmitter. The permit tone is the radio
// core's job, before this engine starts; this module only transmits voice.
#include "config.h"
#if CONFIG_P25_TX

#include <string.h>
#include "board.h"
#include "hal_audio.h"
#include "hal_time.h"
#include "FreeRTOS.h"
#include "task.h"
#include "p25/p25_tx_voice.h"
#include "p25/p25_imbe.h"
#include "p25/tx/p25_live_frame.h"
#include "p25/p25_cc_tx.h"
#if CONFIG_WATCHDOG
#include "app/watchdog.h"
#endif

#define P25_TXV_STACK	4096	// words (16 KB): the fixed-point IMBE encoder nests deeper than the decoder
#define P25_TXV_PRIORITY	3	// host/input priority: the encoder keeps the 20 ms stream, the watchdog kick covers the tight loop
#define P25_TXV_PCMQ	8u	// 160 ms of microphone audio
#define P25_TXV_DRAIN_MS	2000u

// The standard P25 silence IMBE frame; the encoder is not run for a silent 20 ms block.
static const uint8_t p25_silence[11] = { 0x04, 0x0c, 0xfd, 0x7b, 0xfb, 0x7d, 0xf2, 0x7b, 0x3d, 0x9e, 0x44 };

static struct {
	TaskHandle_t task;
	p25_channel_frames frames;
	int16_t pcm[P25_TXV_PCMQ][P25_IMBE_SAMPLES];
	volatile uint16_t head, tail;
	uint8_t payload[9][11];
	uint8_t payload_count;
	uint32_t ldus;
	uint32_t captured, encoded, drops;
	volatile bool running, stopping, started, audio_on;
} v;

static void audio_block(const int16_t *mic, int16_t *spk, void *ctx)
{
	(void)ctx;
	memset(spk, 0, AUDIO_BLOCK_SAMPLES * sizeof *spk);
	if (board.audio->speaker_pcm != NULL) {
		board.audio->speaker_pcm(false);	// keep the beep mixer from re-enabling its 2 ms poll
	}
	if (!v.audio_on || !v.running) {
		return;
	}
	uint16_t head = v.head;
	if ((uint16_t)(head - v.tail) >= P25_TXV_PCMQ) {
		v.drops++;
		return;
	}
	memcpy(v.pcm[head % P25_TXV_PCMQ], mic, P25_IMBE_SAMPLES * sizeof *mic);
	v.head = (uint16_t)(head + 1u);
	v.captured++;
}

static bool publish_ldu(void)
{
	uint8_t ldu[216];
	const uint8_t *base = (v.ldus & 1u) ? v.frames.ldu2 : v.frames.ldu1;

	p25_live_ldu_template(ldu, (const uint8_t (*)[11])v.payload, base);
	if (!board.p25_tx->push_ldu(ldu)) {
		v.payload_count = 0;
		// A full platform queue is a lost 180 ms LDU, not the end of the call: keep producing and let the next one fill
		// the slot. Only a stopped engine ends the call.
		if (board.p25_tx->active()) {
			v.drops++;
			return true;
		}
		return false;
	}
	v.ldus++;
	v.payload_count = 0;
	// The PHY needs two complete LDUs before it can key without underrunning. Do not add startup LDUs just for margin:
	// every one adds 180 ms to the time between PTT and the first RF.
	if (!v.started && v.ldus >= 2u) {
		if (!board.p25_tx->start_live(hal_micros())) {
			return false;
		}
		v.started = true;
	}
	return true;
}

static bool pad_ldu(void)
{
	if (v.payload_count >= 9u) {
		return false;
	}
	memset(v.payload[v.payload_count], 0, sizeof v.payload[0]);
	v.payload_count++;
	return v.payload_count == 9u ? publish_ldu() : true;
}

static void tx_task(void *arg)
{
	(void)arg;
	for (;;) {
		if (v.stopping) {
			if (v.started) {
				while (v.payload_count != 0u) {
					if (!pad_ldu()) {
						break;
					}
				}
				if (v.payload_count >= 9u) {
					v.payload_count = 0;
				}
				if ((v.ldus & 1u) != 0u) {
					memset(v.payload, 0, sizeof v.payload);
					v.payload_count = 9u;
					(void)publish_ldu();
				}
				if (v.ldus >= 2u) {
					(void)board.p25_tx->finish();
				}
				for (uint32_t i = 0; i < P25_TXV_DRAIN_MS && board.p25_tx->active(); i++) {
					vTaskDelay(1);
				}
			}
			board.p25_tx->stop();
			v.running = false;
			v.stopping = false;
			v.task = NULL;
			vTaskDelete(NULL);
			return;
		}
		if (v.tail == v.head) {
			vTaskDelay(1);
			continue;
		}
		int16_t pcm[P25_IMBE_SAMPLES];
		memcpy(pcm, v.pcm[v.tail % P25_TXV_PCMQ], sizeof pcm);
		v.tail = (uint16_t)(v.tail + 1u);
		int32_t peak = 0;
		for (unsigned i = 0; i < P25_IMBE_SAMPLES; i++) {
			int32_t a = pcm[i] < 0 ? -(int32_t)pcm[i] : pcm[i];
			if (a > peak) {
				peak = a;
			}
		}
		if (peak < 64) {
			memcpy(v.payload[v.payload_count], p25_silence, sizeof p25_silence);
		} else {
			p25_imbe_encode(pcm, v.payload[v.payload_count]);
		}
		v.payload_count++;
		v.encoded++;
		// The encoder is a tight CPU loop; without a block the idle task cannot feed the watchdog. Every four frames
		// (80 ms) is enough and costs little of the 20 ms budget.
		if ((v.encoded & 3u) == 0u) {
#if CONFIG_WATCHDOG
			watchdog_kick(hal_millis());
#endif
			vTaskDelay(1);
		}
		if (v.payload_count == 9u && !publish_ldu()) {
			v.stopping = true;
		}
	}
}

static void stop_audio(void)
{
	if (v.audio_on) {
		v.audio_on = false;
		board.audio->stop();
	}
}

bool p25_tx_voice_start(uint32_t hz, uint16_t nac, uint16_t tg, uint32_t source, bool allow_rf)
{
	if (v.running || board.p25_tx == NULL || board.audio == NULL ||
	    !p25_channel_build(&v.frames, nac, tg, source)) {
		return false;
	}
	if (!board.p25_tx->arm_live(hz, 9, false, allow_rf) ||
	    !board.p25_tx->set_frames(v.frames.hdu, v.frames.tdu)) {
		board.p25_tx->disarm();
		return false;
	}
	v.head = v.tail = 0;
	v.payload_count = 0;
	v.ldus = 0;
	v.captured = v.encoded = v.drops = 0;
	v.started = false;
	v.stopping = false;
	v.running = true;
	p25_imbe_reset();
	if (!board.audio->start(audio_block, NULL)) {
		v.running = false;
		board.p25_tx->disarm();
		return false;
	}
	v.audio_on = true;
	if (board.audio->speaker_pcm != NULL) {
		board.audio->speaker_pcm(false);	// the permit beep is over; do not leave the mixer polling every 2 ms
	}
	if (xTaskCreate(tx_task, "p25txv", P25_TXV_STACK, NULL, P25_TXV_PRIORITY, &v.task) != pdPASS) {
		stop_audio();
		v.running = false;
		board.p25_tx->disarm();
		return false;
	}
	return true;
}

void p25_tx_voice_stop(void)
{
	if (!v.running) {
		return;
	}
	v.stopping = true;
	stop_audio();
}

bool p25_tx_voice_active(void)
{
	return v.running || (board.p25_tx != NULL && board.p25_tx->active());
}

void p25_tx_voice_stats(uint32_t *captured, uint32_t *encoded, uint32_t *ldus, uint32_t *drops)
{
	*captured = v.captured;
	*encoded = v.encoded;
	*ldus = v.ldus;
	*drops = v.drops;
}

static bool cc_uplink_hz(const settings_p25_t *p25, uint32_t *hz)
{
	uint32_t f = p25->uplink_hz;
	if (f == 0u && p25->cc_hz > 5000000u) {
		f = p25->cc_hz - 5000000u;
	}
	*hz = f;
	return f != 0u;
}

bool p25_cc_burst_start(const settings_p25_t *p25, uint16_t frames)
{
	if (v.running || board.p25_tx == NULL || p25 == NULL || frames == 0u) {
		return false;
	}
	uint32_t hz;
	if (!cc_uplink_hz(p25, &hz)) {
		return false;
	}
	static uint8_t stream[60u * P25_CC_FRAME_BYTES];
	uint8_t tsbk[P25_CC_TSBK_BYTES];
	size_t n = 0;
	for (uint16_t i = 0; i < frames && n + P25_CC_FRAME_BYTES <= sizeof stream; i++) {
		if ((i & 1u) == 0u) {
			p25_tsbk_unit_registration(tsbk, p25->wacn, p25->sys, p25->unit_id, false, 0);
		} else {
			p25_tsbk_group_affiliation(tsbk, p25->sys, p25->tg, p25->unit_id);
		}
		p25_cc_build_frame(&stream[n], p25->nac, tsbk);
		n += P25_CC_FRAME_BYTES;
	}
	if (!board.p25_tx->arm(hz, 9, false, true) ||
	    !board.p25_tx->start_raw(stream, (uint32_t)n, hal_micros())) {
		return false;
	}
	return true;
}

bool p25_cc_affiliate_start(const settings_p25_t *p25)
{
	if (v.running || board.p25_tx == NULL || p25 == NULL) {
		return false;
	}
	uint32_t hz;
	if (!cc_uplink_hz(p25, &hz)) {
		return false;
	}
	// The proven radio-local registration burst: enough repeats for the site's lossy uplink decoder.
	static uint8_t stream[60u * P25_CC_FRAME_BYTES];
	uint8_t tsbk[P25_CC_TSBK_BYTES];
	size_t n = 0;
	for (unsigned round = 0; round < 30u; round++) {
		p25_tsbk_unit_registration(tsbk, p25->wacn, p25->sys, p25->unit_id, false, 0);
		p25_cc_build_frame(&stream[n], p25->nac, tsbk);
		n += P25_CC_FRAME_BYTES;
		p25_tsbk_group_affiliation(tsbk, p25->sys, p25->tg, p25->unit_id);
		p25_cc_build_frame(&stream[n], p25->nac, tsbk);
		n += P25_CC_FRAME_BYTES;
	}
	return board.p25_tx->arm(hz, 9, false, true) &&
	       board.p25_tx->start_raw(stream, (uint32_t)n, hal_micros());
}

bool p25_cc_deregister_start(const settings_p25_t *p25)
{
	if (v.running || board.p25_tx == NULL || p25 == NULL) {
		return false;
	}
	uint32_t hz;
	if (!cc_uplink_hz(p25, &hz)) {
		return false;
	}
	static uint8_t stream[60u * P25_CC_FRAME_BYTES];
	uint8_t tsbk[P25_CC_TSBK_BYTES];
	size_t n = 0;
	for (unsigned i = 0; i < 60u; i++) {
		p25_tsbk_unit_deregistration(tsbk, p25->unit_id);
		p25_cc_build_frame(&stream[n], p25->nac, tsbk);
		n += P25_CC_FRAME_BYTES;
	}
	return board.p25_tx->arm(hz, 9, false, true) &&
	       board.p25_tx->start_raw(stream, (uint32_t)n, hal_micros());
}

bool p25_cc_voice_request_start(const settings_p25_t *p25)
{
	if (v.running || board.p25_tx == NULL || p25 == NULL) {
		return false;
	}
	uint32_t hz;
	if (!cc_uplink_hz(p25, &hz)) {
		return false;
	}
	static uint8_t stream[8u * P25_CC_FRAME_BYTES];
	uint8_t tsbk[P25_CC_TSBK_BYTES];
	size_t n = 0;
	for (unsigned i = 0; i < 4u; i++) {
		p25_tsbk_group_affiliation_query_response(tsbk, p25->tg, p25->unit_id);
		p25_cc_build_frame(&stream[n], p25->nac, tsbk);
		n += P25_CC_FRAME_BYTES;
	}
	for (unsigned i = 0; i < 4u; i++) {
		p25_tsbk_group_voice_request(tsbk, p25->tg, p25->unit_id, 0);
		p25_cc_build_frame(&stream[n], p25->nac, tsbk);
		n += P25_CC_FRAME_BYTES;
	}
	return board.p25_tx->arm(hz, 9, false, true) &&
	       board.p25_tx->start_raw(stream, (uint32_t)n, hal_micros());
}

#endif
