// SPDX-License-Identifier: MIT
// Copyright (c) 2026 The RadioFoundry Authors
// FreeRTOS settings this radio changes from platform/hrc7000/FreeRTOSConfig.h (read before its defaults)
#ifndef FREERTOS_BOARD_H
#define FREERTOS_BOARD_H

// The RTOS heap holds every task stack and queue. The P25/FM image links 75,264 B of the unused codec window above the
// old 211,456 B region (boards/dm32/p25_only.ld, config.mk): 116 KB of that is the heap, about 11.5 KB stays static for
// other buffers. The extra room covers the P25 encoder task, a second voice/call task and later encryption work. A DMR
// build keeps the old layout and heap. The debug link's tasks view reports the least free since start
// (tasks.heap_min_free)
#if !CONFIG_DMR
#define configTOTAL_HEAP_SIZE	(116 * 1024)
#elif CONFIG_P25_TX
// The live P25 encoder needs its own task stack in addition to the ordinary tasks; keep headroom for a second
// audio/call task and a zone share while it runs.
#define configTOTAL_HEAP_SIZE	(52 * 1024)
#else
#define configTOTAL_HEAP_SIZE	(33 * 1024)
#endif

#endif
