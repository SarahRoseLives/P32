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
#include "p25/p25_voice.h"
#include "p25/p25_demod.h"
#include "p25/p25_rx.h"
#include "p25/p25_call.h"
#include "p25/p25_receive_policy.h"
#include "p25/p25_voice_policy.h"
#include "p25/p25_imbe.h"

#define P25_VOICE_PCM_FRAMES	32u	// 640 ms: two LDUs plus jitter
#define P25_VOICE_PREFILL	16u	// start after ~320 ms so a late LDU never gaps the speaker
#define P25_VOICE_STACK		1024	// words: the demodulator and the codec run in this task

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
	uint16_t peak;
} v;

// DC blocker and AGC/limiter: the fixed-point decoder's output is quiet (peak a few hundred for speech), so boost
// it to a steady level and saturate instead of clipping. The gate keeps codec idle noise from being amplified.
#define P25_AGC_TARGET		12000
#define P25_AGC_GATE		24
#define P25_AGC_MAX_Q8		8192	// 32x
static void audio_process(int16_t pcm[P25_IMBE_SAMPLES])
{
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
		v.agc_q8 += (desired - v.agc_q8) >> 4;	// ~320 ms in either direction
	} else {
		v.agc_q8 += (256 - v.agc_q8) >> 6;
	}
	if (peak < 16) {
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
}

static void on_frame(const p25_rx *f, void *ctx)
{
	p25_call_metadata meta;
	p25_receive_event event;

	(void)ctx;
	v.units++;
	bool valid = p25_call_decode(f->frame, f->duid, &meta);
	event = p25_receive_update(&v.call, f->nac, f->duid, &meta, valid, hal_micros());
	if (event.reset) {
		p25_imbe_reset();
		p25_voice_policy_init(&v.quality);
		v.hp_x1 = v.hp_y1 = 0;
		v.agc_q8 = 256;
		v.env = 0;
		v.playing = false;
		v.starved = 0;
	}
	if (event.discard) {
		v.head = v.tail;	// a new/encrypted/unknown call never keeps old speech queued
		v.playing = false;
		v.starved = 0;
	}
	if (f->duid == 3 || f->duid == 15) {
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
			p25_imbe_decode(bits, pcm);
			audio_process(pcm);
			memcpy(v.last, pcm, sizeof v.last);
			v.accepted++;
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
				p25_demod_push(&v.demod, samples[i], on_frame, NULL);
			}
		}
		// Yield every chunk: the demodulator's sync search is CPU heavy and must not starve the watchdog feeder
		vTaskDelay(1);
	}
	v.task = NULL;
	vTaskDelete(NULL);
}

bool p25_voice_start(void)
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
	p25_demod_init(&v.demod);
	p25_receive_reset(&v.call);
	p25_voice_policy_init(&v.quality);
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
	dlog("p25v stop n %u v %u over %u under %u", (unsigned)v.units, (unsigned)v.voices,
	     (unsigned)v.overflows, (unsigned)v.underruns);
}

bool p25_voice_running(void)
{
	return v.running;
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
	out->units = v.units;
	out->voices = v.voices;
	out->corrected = v.corrected;
	out->nac = v.call.nac;
	out->group = v.call.group;
	out->state = (uint8_t)v.call.state;
	out->underruns = v.underruns;
	out->overflows = v.overflows;
}

#endif
