// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 The RadioFoundry Authors
// Continuous P25 control-channel receive (docs/spec/p25.md 8): the 8 kHz discriminator feeds the software P25
// demodulator, and each validated control-channel frame updates the monitor's last decoded values. The receiver
// keeps running through NID errors; only a real carrier loss is an out-of-range condition.
#include "config.h"
#if CONFIG_P25

#include <string.h>
#include "board.h"
#include "hal_audio.h"
#include "FreeRTOS.h"
#include "task.h"
#include "p25/p25_cc_rx.h"
#include "p25/p25_demod.h"
#include "p25/p25_tsbk.h"
#include "p25/p25_tsbk_parse.h"
#include "hal_time.h"
#if CONFIG_P25_VOICE
#include "p25/p25_voice.h"
#else
static p25_demod cc_demod;
#endif

#define P25_CC_STACK	1152	// words (4.5 KB): the demodulator, TSBK trellis and the IMBE decode it can reach
#define P25_CC_PRIORITY	4
#define P25_CC_PARTIAL	180u	// one control-channel frame: FSW, NID, TSBK and status symbols

static struct {
	p25_demod *demod;
	p25_cc_t cc;
	volatile uint32_t seq;
	hal_p25_cc_t out;
	volatile uint32_t frames;
	volatile uint32_t activity;
	uint32_t last_acq;
	uint32_t progress_ms;
	bool running;
	TaskHandle_t task;
} v;

static void publish(void)
{
	v.seq++;
	v.out.valid = true;
	v.out.opcode = v.cc.opcode;
	v.out.nac = v.cc.nac;
	v.out.duid = 7;		// control channel
	v.out.wacn = v.cc.wacn;
	v.out.sys = v.cc.sys;
	v.out.rfss = v.cc.rfss;
	v.out.site = v.cc.site;
	v.out.tg = v.cc.tg;
	v.out.vch = v.cc.vch;
	v.out.freq_hz = v.cc.freq_hz;
	v.out.voice_hz = v.cc.voice_hz;
	v.out.voice_tx_hz = v.cc.voice_tx_hz;
	v.out.voice_id = v.cc.voice_id;
	v.out.voice_grant = v.cc.voice_grant;
	v.out.source = v.cc.source;
	v.out.grants = v.cc.grant_seq;
	v.out.affiliated = v.cc.affiliation_seq != 0;
	v.out.affiliate_tg = v.cc.affiliate_tg;
	v.out.affiliate_source = v.cc.affiliate_source;
	v.out.affiliation_seq = v.cc.affiliation_seq;
	v.out.blocks = v.frames;
	v.out.activity = v.activity;
	v.seq++;
}

static bool decode_tsbk(const p25_rx *f)
{
	uint8_t bits[P25_TSBK_BITS];
	uint8_t tsbk[P25_TSBK_BYTES];
	unsigned n = 0;

	// The TSBK follows the 24-dibit FSW and the 33-dibit NID. The on-air stream has a status symbol after every
	// 35 data dibits; the software receiver has the raw stream, so these are the only symbols to skip.
	for (unsigned g = 57; g < P25_CC_PARTIAL && n < P25_TSBK_BITS; g++) {
		if (g >= 35u && ((g - 35u) % 36u) == 0u) {
			continue;
		}
		uint8_t d = (uint8_t)((f->frame[g / 4u] >> (6u - 2u * (g % 4u))) & 3u);
		bits[n++] = (uint8_t)((d >> 1) & 1u);
		bits[n++] = (uint8_t)(d & 1u);
	}
	if (n != P25_TSBK_BITS || p25_tsbk_decode(bits, NULL, tsbk) < 0) {
		return false;
	}
	return p25_tsbk_parse(tsbk, &v.cc);
}

static void on_frame(const p25_rx *f, void *ctx)
{
	(void)ctx;
	if (f->duid != 7u) {
		return;
	}
	v.cc.nac = f->nac;
	v.frames++;
	v.progress_ms = hal_millis();
	if (!decode_tsbk(f)) {
		// The NID is a valid control channel even when this TSBK's trellis/CRC does not check. Keep the last
		// decoded fields, but refresh the header and the decoded-frame count.
		v.cc.opcode = 0;
	}
	publish();
}

static void cc_task(void *arg)
{
	int16_t samples[AUDIO_BLOCK_SAMPLES];

	(void)arg;
	for (;;) {
		if (!v.running) {
			vTaskDelay(10);
			continue;
		}
		uint16_t n = board.p25_probe->audio_read(samples, AUDIO_BLOCK_SAMPLES);

		if (n) {
			for (uint16_t i = 0; i < n && v.running; i++) {
				p25_demod_push(v.demod, samples[i], on_frame, NULL);
				if ((i & 31u) == 31u) {
					vTaskDelay(1);
				}
			}
			uint32_t acq = v.demod->acquisitions;
			uint32_t now = hal_millis();
			if (acq > v.last_acq) {
				v.activity += acq - v.last_acq;
				v.last_acq = acq;
				v.progress_ms = now;
				v.seq++;
				v.out.activity = v.activity;
				v.seq++;
			}
			// A false NID can leave a lane waiting for a frame length that never arrives. The voice demod's own
			// hunting re-arm refuses to touch such a lane; reset the whole demod after a quiet second.
			if ((uint32_t)(now - v.progress_ms) > 1200u) {
				p25_demod_init(v.demod);
				v.last_acq = 0;
				v.progress_ms = now;
			}
		}
		vTaskDelay(1);
	}
}

bool p25_cc_rx_start(void)
{
	if (v.running) {
		return true;
	}
	if (board.p25_probe == NULL || board.p25_probe->audio_start == NULL ||
	    board.p25_probe->audio_read == NULL || board.p25_probe->audio_stop == NULL) {
		return false;
	}
	if (v.task == NULL) {
#if CONFIG_P25_VOICE
		p25_demod *demod = p25_voice_demod();
#else
		p25_demod *demod = &cc_demod;
#endif
		v.demod = demod;
		if (xTaskCreate(cc_task, "p25cc", P25_CC_STACK, NULL, P25_CC_PRIORITY, &v.task) != pdPASS) {
			v.task = NULL;
			return false;
		}
	}
	v.seq = 0;
	memset(&v.cc, 0, sizeof v.cc);
	memset(&v.out, 0, sizeof v.out);
	v.frames = 0;
	v.activity = 0;
	v.last_acq = 0;
	v.progress_ms = hal_millis();
	p25_demod_init(v.demod);
	if (!board.p25_probe->audio_start()) {
		return false;
	}
	v.running = true;
	return true;
}

void p25_cc_rx_stop(void)
{
	if (!v.running) {
		return;
	}
	v.running = false;
	board.p25_probe->audio_stop();
}

bool p25_cc_rx_running(void)
{
	return v.running;
}

bool p25_cc_rx_last(hal_p25_cc_t *out)
{
	uint32_t a, b = 0;

	do {
		a = v.seq;
		if (a & 1u) {
			continue;
		}
		*out = v.out;
		b = v.seq;
	} while (a != b);
	out->blocks = v.frames;
	out->activity = v.activity;
	return out->valid;
}
#endif
