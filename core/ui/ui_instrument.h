// SPDX-License-Identifier: MIT
// Copyright (c) 2026 The RadioFoundry Authors
// Optional large-panel Home: one channel instrument for FM, conventional P25 and trunked P25.
#ifndef UI_INSTRUMENT_H
#define UI_INSTRUMENT_H

#include <stdbool.h>
#include "config.h"
#include "radio/radio.h"
#include "ui/ui_screens.h"

#if CONFIG_UI_INSTRUMENT
// Draw the instrument Home when this panel is wide/tall enough and the current channel is FM or P25.
// False leaves Home to the standard drawing.
bool ui_instrument_home(const radio_view_t *v, const ui_state_t *s);
#else
static inline bool ui_instrument_home(const radio_view_t *v, const ui_state_t *s)
{
	(void)v;
	(void)s;
	return false;
}
#endif

#endif
