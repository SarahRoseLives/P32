// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 The RadioFoundry Authors
// Conventional P25 Phase 1 voice receive (p25_voice.h, docs/spec/p25.md 6, 9): the FM discriminator (p25_demod) to
// validated LDUs (p25_rx), call/encryption state (p25_call, p25_receive_policy), voice FEC quality (p25_voice_policy)
// and the fixed-point IMBE decoder to a PCM ring the audio task drains. RX only: no transmitter is touched.
#include "config.h"
#if CONFIG_P25_VOICE

#include <string.h>
#include "board.h"
#include "hal_time.h"
#include "hal_audio.h"
#include "FreeRTOS.h"
#include "task.h"
#include "debuglink/dlog.h"
#if CONFIG_WATCHDOG
#include "app/watchdog.h"
#endif
#include "p25/p25_voice.h"
#include "p25/p25_demod.h"
#include "p25/p25_rx.h"
#include "p25/p25_call.h"
#include "p25/p25_receive_policy.h"
#include "p25/p25_voice_policy.h"
#include "p25/p25_imbe.h"
#if CONFIG_BEEP
#include "radio/beep.h"
#endif

#define P25_VOICE_PCM_FRAMES	48u	// 960 ms: three LDUs plus jitter
#define P25_VOICE_PREFILL	18u	// start after ~360 ms so a late LDU never gaps the speaker
#define P25_VOICE_STACK		1536	// words (6 KB): the demodulator, policies and codec nest; stack-check needs 3896 B

static struct {
	p25_demod demod;
	p25_receive_policy call;
	p25_voice_policy quality;
	int16_t pcm[P25_VOICE_PCM_FRAMES][P25_IMBE_SAMPLES];
	uint16_t head, tail;		// frame counters: the decode task writes head, the audio task tail
	int16_t last[P25_IMBE_SAMPLES];
	volatile bool running;
	volatile bool playing;		// the prefilled ring is feeding the speaker
	uint8_t starved;		// consecutive empty blocks since the last frame
	int32_t env;			// smoothed peak for the AGC
	int32_t hp_x1, hp_y1;		// DC blocker state
	int32_t agc_q8;			// Q8 gain, 256 = unity
	TaskHandle_t task;
	uint32_t samples, units, voices, corrected, underruns, overflows, accepted, repeated, muted;
	uint32_t max_push_us, max_frame_us, resets;
	uint16_t f_nac, f_tg;		// grant filter (0 = any)
	bool filter;
	volatile bool ended;		// a terminator was seen
	bool agc_off;		// play the codec output as-is (default: no boost)
	uint32_t logged;
	uint32_t started_ms, last_voice_ms;
	uint16_t peak;
} v;

// DC blocker and AGC/limiter: the fixed-point decoder's output is quiet (peak a few hundred for speech), so boost
// it to a steady level and saturate instead of clipping. The gate keeps codec idle noise from being amplified.
#define P25_AGC_TARGET		9000
#define P25_AGC_GATE		32
#define P25_AGC_MAX_Q8		3072	// 12x: the codec is quiet, but 32x amplifies its noise
static void audio_process(int16_t pcm[P25_IMBE_SAMPLES])
{
	if (v.agc_off) {
		return;	// reference-like: no DC blocker, AGC or gate
	}
	int32_t tmp[P25_IMBE_SAMPLES];
	int32_t peak = 0;

	for (unsigned i = 0; i < P25_IMBE_SAMPLES; i++) {
		int32_t y = (int32_t)pcm[i] - v.hp_x1 + v.hp_y1 - (v.hp_y1 >> 5);	// ~40 Hz high-pass
		v.hp_x1 = pcm[i];
		if (y > 32767) y = 32767;
		if (y < -32768) y = -32768;
		v.hp_y1 = y;
		tmp[i] = y;
		int32_t a = y < 0 ? -y : y;
		if (a > peak) peak = a;
	}
	// Track the long-term level, not per-frame peaks, so syllables do not pump the gain
	if (peak > v.env) {
		v.env += (peak - v.env) >> 1;
	} else {
		v.env -= v.env >> 6;
	}
	if (v.env >= P25_AGC_GATE) {
		int32_t desired = (P25_AGC_TARGET << 8) / v.env;
		if (desired < 256) desired = 256;
		if (desired > P25_AGC_MAX_Q8) desired = P25_AGC_MAX_Q8;
		v.agc_q8 += (desired - v.agc_q8) >> 5;	// ~640 ms: syllables do not pump the gain
	} else {
		v.agc_q8 += (256 - v.agc_q8) >> 6;
	}
	if (peak < 32) {
		memset(pcm, 0, P25_IMBE_SAMPLES * sizeof(int16_t));	// idle: never amplify codec noise
		return;
	}
	for (unsigned i = 0; i < P25_IMBE_SAMPLES; i++) {
		int32_t s = (tmp[i] * v.agc_q8) >> 8;
		if (s > 30000) s = 30000;
		if (s < -30000) s = -30000;
		pcm[i] = (int16_t)s;
	}
}

static void pcm_push(const int16_t pcm[P25_IMBE_SAMPLES])
{
	uint16_t n = (uint16_t)(v.head - v.tail);

	if (n >= P25_VOICE_PCM_FRAMES) {
		v.overflows++;
		return;
	}
	memcpy(v.pcm[v.head % P25_VOICE_PCM_FRAMES], pcm, sizeof v.pcm[0]);
	v.head = (uint16_t)(v.head + 1);
}

static void audio_block(const int16_t *mic, int16_t *spk, void *ctx)
{
	(void)mic;
	(void)ctx;
	uint16_t n = (uint16_t)(v.head - v.tail);

	if (!v.playing && n >= P25_VOICE_PREFILL) {
		v.playing = true;
	}
	if (v.playing && n != 0) {
		memcpy(spk, v.pcm[v.tail % P25_VOICE_PCM_FRAMES], AUDIO_BLOCK_SAMPLES * sizeof(int16_t));
		memcpy(v.last, spk, sizeof v.last);
		v.tail = (uint16_t)(v.tail + 1);
		v.starved = 0;
	} else if (v.playing && v.starved < 3u) {
		memcpy(spk, v.last, AUDIO_BLOCK_SAMPLES * sizeof(int16_t));	// conceal a short gap, then silence
		v.starved++;
		v.underruns++;
	} else {
		memset(spk, 0, AUDIO_BLOCK_SAMPLES * sizeof(int16_t));
		if (v.playing) {
			v.playing = false;
			v.underruns++;	// only a real gap; the prefill is silence, not an underrun
		}
	}
#if CONFIG_BEEP
	beep_mix(spk);		// the permit chirp and refusal bonk share this stream
#endif
}

static void on_frame(const p25_rx *f, void *ctx)
{
	p25_call_metadata meta;
	p25_receive_event event;

	(void)ctx;
	if (f->duid == 7u) {
		return;		// a control-channel frame from a nearby system, not voice
	}
	bool valid = p25_call_decode(f->frame, f->duid, &meta);

	if (v.filter) {
		if (f->nac != v.f_nac) {
			return;		// another system on the voice channel
		}
		if (v.f_tg != 0 && valid && meta.group_valid && meta.group != v.f_tg) {
			return;		// another talkgroup
		}
	}
	v.units++;
	event = p25_receive_update(&v.call, f->nac, f->duid, &meta, valid, hal_micros());
	if (event.reset) {
		v.resets++;
		p25_imbe_reset();
		p25_voice_policy_init(&v.quality);
		v.hp_x1 = v.hp_y1 = 0;
		v.agc_q8 = 256;
		v.env = 0;
		v.playing = false;
		v.starved = 0;
	}
	if (event.discard) {
		v.head = v.tail;
		v.playing = false;
		v.starved = 0;
	}
	if (f->duid == 3 || f->duid == 15) {
		v.ended = true;
		return;
	}
	if ((f->duid != 5 && f->duid != 10) || !event.voice) {
		return;
	}
	for (unsigned i = 0; i < 9u; i++) {
		uint8_t bits[P25_IMBE_BYTES];
		int16_t pcm[P25_IMBE_SAMPLES];
		p25_voice_quality quality;

		if (!p25_rx_voice_quality(f->frame, i, bits, &quality)) {
			break;
		}
		p25_voice_action action = p25_voice_policy_frame(&v.quality, bits, quality.e0, quality.et);
		if (action == P25_VOICE_ACCEPT) {
			uint32_t t0 = hal_micros();
			p25_imbe_decode(bits, pcm);
			uint32_t dt = hal_micros() - t0;
			if (dt > v.max_frame_us) {
				v.max_frame_us = dt;
			}
			if (v.logged < 6u) {
				int16_t pk = 0;
				for (unsigned j = 0; j < P25_IMBE_SAMPLES; j++) {
					int16_t a = pcm[j] < 0 ? (int16_t)-pcm[j] : pcm[j];
					if (a > pk) pk = a;
				}
				dlog("p25v cw %02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x pk %u et %u", bits[0], bits[1],
				     bits[2], bits[3], bits[4], bits[5], bits[6], bits[7], bits[8], bits[9], bits[10],
				     (unsigned)pk, quality.et);
				v.logged++;
			}
			audio_process(pcm);
			memcpy(v.last, pcm, sizeof v.last);
			v.accepted++;
			v.last_voice_ms = hal_millis();
			for (unsigned j = 0; j < P25_IMBE_SAMPLES; j++) {
				int16_t a = pcm[j] < 0 ? (int16_t)-pcm[j] : pcm[j];
				if (a > (int16_t)v.peak) {
					v.peak = (uint16_t)a;
				}
			}
		} else if (action == P25_VOICE_REPEAT) {
			memcpy(pcm, v.last, sizeof pcm);
			v.repeated++;
		} else {
			memset(pcm, 0, sizeof pcm);
			v.muted++;
		}
		pcm_push(pcm);
		v.voices++;
		v.corrected += quality.et;
		vTaskDelay(1);	// one yield per codec frame: bounds the burst without slowing the demod
	}
}

static void decode_task(void *arg)
{
	int16_t samples[AUDIO_BLOCK_SAMPLES];

	(void)arg;
	while (v.running) {
		uint16_t n = board.p25_probe->audio_read(samples, AUDIO_BLOCK_SAMPLES);

		if (n) {
			v.samples += n;
			for (uint16_t i = 0; i < n && v.running; i++) {
				uint32_t t0 = hal_micros();
				p25_demod_push(&v.demod, samples[i], on_frame, NULL);
				uint32_t dt = hal_micros() - t0;
				if (dt > v.max_push_us) {
					v.max_push_us = dt;
				}
				if ((i & 31u) == 31u) {
					vTaskDelay(1);
				}
			}
		}
		// Yield every chunk: the demodulator's sync search is CPU heavy and must not starve the watchdog feeder
		vTaskDelay(1);
	}
	v.task = NULL;
	vTaskDelete(NULL);
}

static bool start_common(uint16_t nac, uint16_t tg)
{
	if (v.running) {
		return true;
	}
	if (board.p25_probe == NULL || board.p25_probe->audio_start == NULL || board.p25_probe->audio_read == NULL ||
	    board.audio == NULL) {
		dlog("p25v: no path probe=%d start=%d read=%d audio=%d", board.p25_probe != NULL,
		     board.p25_probe && board.p25_probe->audio_start != NULL,
		     board.p25_probe && board.p25_probe->audio_read != NULL, board.audio != NULL);
		return false;
	}
	memset(&v, 0, sizeof v);
	v.agc_off = false;
	p25_demod_init(&v.demod);
	p25_receive_reset(&v.call);
	p25_voice_policy_init(&v.quality);
	v.f_nac = nac;
	v.f_tg = tg;
	v.filter = nac != 0 || tg != 0;
	v.started_ms = hal_millis();
	if (!board.p25_probe->audio_start()) {
		dlog("p25v: discriminator start refused");
		return false;
	}
	if (!board.audio->start(audio_block, NULL)) {
		dlog("p25v: pcm start refused heap %u", (unsigned)xPortGetFreeHeapSize());
		board.p25_probe->audio_stop();
		return false;
	}
	if (board.audio->speaker_pcm != NULL) {
		board.audio->speaker_pcm(true);
	}
	v.running = true;
	if (xTaskCreate(decode_task, "p25v", P25_VOICE_STACK, NULL, 4, &v.task) != pdPASS) {
		dlog("p25v: no task heap %u", (unsigned)xPortGetFreeHeapSize());
		v.running = false;
		board.audio->stop();
		board.p25_probe->audio_stop();
		return false;
	}
	dlog("p25v start");
	return true;
}

void p25_voice_set_agc(bool on)
{
	v.agc_off = !on;
}

bool p25_voice_agc(void)
{
	return !v.agc_off;
}

bool p25_voice_start(void)
{
	return start_common(0, 0);
}

bool p25_voice_start_filtered(uint16_t nac, uint16_t tg)
{
	return start_common(nac, tg);
}

bool p25_voice_ended(void)
{
	uint32_t now;

	if (!v.running) {
		return true;
	}
	if (v.ended) {
		v.ended = false;
		return true;
	}
	now = hal_millis();
	if (v.voices != 0 && (uint32_t)(now - v.last_voice_ms) > 1500u) {
		return true;
	}
	if (v.units == 0 && (uint32_t)(now - v.started_ms) > 3000u) {
		return true;	// nothing on this voice channel
	}
	return false;
}

void p25_voice_stop(void)
{
	if (!v.running) {
		return;
	}
	v.running = false;
	board.audio->stop();
	if (board.audio->speaker_pcm != NULL) {
		board.audio->speaker_pcm(false);
	}
	board.p25_probe->audio_stop();
	if (v.task != NULL) {
		vTaskDelete(v.task);
		v.task = NULL;
	}
	dlog("p25v stop n %u v %u a %u r %u m %u res %u und %u ov %u", (unsigned)v.units,
	     (unsigned)v.voices, (unsigned)v.accepted, (unsigned)v.repeated, (unsigned)v.muted, (unsigned)v.resets,
	     (unsigned)v.underruns, (unsigned)v.overflows);
}

bool p25_voice_running(void)
{
	return v.running;
}

p25_demod *p25_voice_demod(void)
{
	return &v.demod;
}

void p25_voice_stats(p25_voice_stats_t *out)
{
	out->samples = v.samples;
	out->sync_tests = v.demod.sync_tests;
	out->acquisitions = v.demod.acquisitions;
	out->accepted = v.accepted;
	out->repeated = v.repeated;
	out->muted = v.muted;
	out->peak = v.peak;
	out->resets = v.resets;
	out->drops = (board.p25_probe != NULL && board.p25_probe->audio_dropped != NULL)
		? board.p25_probe->audio_dropped() : 0;
	out->max_push_us = v.max_push_us;
	out->resets = v.resets;
	out->drops = (board.p25_probe != NULL && board.p25_probe->audio_dropped != NULL)
		? board.p25_probe->audio_dropped() : 0;
	out->max_frame_us = v.max_frame_us;
	out->units = v.units;
	out->voices = v.voices;
	out->corrected = v.corrected;
	out->nac = v.call.nac;
	out->group = v.call.group;
	out->source = v.call.source;
	out->state = (uint8_t)v.call.state;
	out->underruns = v.underruns;
	out->overflows = v.overflows;
}

#endif
