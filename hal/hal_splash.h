// SPDX-License-Identifier: MIT
// Copyright (c) 2026 The RadioFoundry Authors
// A board's optional bitmap boot screen: 4 bits per pixel, row major, high nibble first, 16 RGB565 palette entries.
#ifndef HAL_SPLASH_H
#define HAL_SPLASH_H

#include <stdint.h>

typedef struct {
	uint16_t width, height;
	uint8_t colors;			// entries used in palette (1..16)
	const uint16_t *palette;	// RGB565
	const uint8_t *pixels;		// two pixels per byte, high nibble first
} hal_splash_t;

#endif
