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

#define P25_VOICE_PCM_FRAMES	10u	// 200 ms: one LDU (9 frames) plus the audio task draining it
#define P25_VOICE_STACK		1024	// words: the demodulator and the codec run in this task

static struct {
	p25_demod demod;
	p25_receive_policy call;
	p25_voice_policy quality;
	int16_t pcm[P25_VOICE_PCM_FRAMES][P25_IMBE_SAMPLES];
	uint16_t head, tail;		// frame counters: the decode task writes head, the audio task tail
	int16_t last[P25_IMBE_SAMPLES];
	volatile bool running;
	TaskHandle_t task;
	uint32_t samples, units, voices, corrected, underruns, overflows;
} v;

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
	if (v.tail != v.head) {
		memcpy(spk, v.pcm[v.tail % P25_VOICE_PCM_FRAMES], AUDIO_BLOCK_SAMPLES * sizeof(int16_t));
		memcpy(v.last, spk, sizeof v.last);
		v.tail = (uint16_t)(v.tail + 1);
	} else {
		memcpy(spk, v.last, AUDIO_BLOCK_SAMPLES * sizeof(int16_t));	// brief concealment: repeat the last frame
		v.underruns++;
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
	}
	if (event.discard) {
		v.head = v.tail;	// a new/encrypted/unknown call never keeps old speech queued
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
			memcpy(v.last, pcm, sizeof v.last);
		} else if (action == P25_VOICE_REPEAT) {
			memcpy(pcm, v.last, sizeof pcm);
		} else {
			memset(pcm, 0, sizeof pcm);
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
	out->units = v.units;
	out->voices = v.voices;
	out->corrected = v.corrected;
	out->underruns = v.underruns;
	out->overflows = v.overflows;
}

#endif
