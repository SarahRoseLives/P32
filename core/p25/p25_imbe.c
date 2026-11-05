// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 The RadioFoundry Authors
// P25 Phase 1 IMBE decode bench: decode known codewords and log the per-frame cost (hal_micros), to measure the CK803S
// against the 20 ms speech period before the RX path is built (docs/spec/p25.md 9). The codec itself is the fixed-point
// vocoder in core/p25/imbe, wrapped by p25_imbe_codec.cpp.
#include "config.h"
#if CONFIG_P25_VOICE

#include <stdint.h>
#include "hal_time.h"
#include "debuglink/dlog.h"
#include "FreeRTOS.h"
#include "task.h"
#include "p25/p25_imbe.h"

// Two codewords from the 1011 Hz LDU fixture (fixtures/p25-1011), the pair the host decode validates
static const uint8_t bench_frames[][P25_IMBE_BYTES] = {
	{ 0x09, 0xb0, 0x88, 0x0c, 0xc6, 0x21, 0xf6, 0x80, 0xa8, 0x26, 0x00 },
	{ 0x09, 0xb0, 0x88, 0x0c, 0xc6, 0x21, 0xf6, 0x80, 0xa8, 0x26, 0x01 },
};

#define P25_IMBE_BENCH_FRAMES	24
#define P25_IMBE_BENCH_STACK	1024	// words (4 KB): the fixed-point synthesis is iterative, unlike mbelib's float path

static uint32_t fnv_pcm(const int16_t pcm[P25_IMBE_SAMPLES])
{
	uint32_t h = 2166136261u;

	for (int i = 0; i < P25_IMBE_SAMPLES; i++) {
		h ^= (uint8_t)(pcm[i] & 0xff);
		h *= 16777619u;
		h ^= (uint8_t)((pcm[i] >> 8) & 0xff);
		h *= 16777619u;
	}
	return h;
}

static void bench_task(void *arg)
{
	int16_t pcm[P25_IMBE_SAMPLES];
	uint32_t total = 0, worst = 0, over15 = 0, over20 = 0, fnv = 0;

	(void)arg;
	p25_imbe_reset();
	for (unsigned i = 0; i < P25_IMBE_BENCH_FRAMES; i++) {
		uint32_t t0 = hal_micros();
		p25_imbe_decode(bench_frames[i % 2], pcm);
		uint32_t dt = hal_micros() - t0;

		if (i == 0) {
			fnv = fnv_pcm(pcm);
		}
		total += dt;
		if (dt > worst) {
			worst = dt;
		}
		if (dt > 15000u) {
			over15++;
		}
		if (dt > 20000u) {
			over20++;
		}
		vTaskDelay(1);	// let the idle task feed the watchdog (and the host answer) between frames
	}
	dlog("p25imbe n %u mean %u us max %u us >15ms %u >20ms %u fnv %08x",
	     (unsigned)P25_IMBE_BENCH_FRAMES, (unsigned)(total / P25_IMBE_BENCH_FRAMES), (unsigned)worst,
	     (unsigned)over15, (unsigned)over20, (unsigned)fnv);
	vTaskDelete(NULL);
}

void p25_imbe_bench(void)
{
	if (xTaskCreate(bench_task, "imbe", P25_IMBE_BENCH_STACK, NULL, tskIDLE_PRIORITY + 1, NULL) != pdPASS) {
		dlog("p25imbe: no stack for the bench");
	}
}

#endif
