// SPDX-License-Identifier: MIT
// Copyright (c) 2026 The RadioFoundry Authors
// The P25 views: the radio task reads the probe's last decoded control channel (CONFIG_P25) or the voice receiver's
// state and counters (CONFIG_P25_VOICE) into the radio view while its screen is open (docs/spec/p25.md 6, 8, 9).
#ifndef RADIO_P25_H
#define RADIO_P25_H

#include "config.h"
#include "radio/radio.h"

#if CONFIG_P25
// Read the probe's last decoded control channel into v (its on flag is left as the state machine set it). Called from
// the radio task's poll; runs the decode in that task. Returns true when a shown field changed.
bool radio_p25_publish(radio_p25_view_t *v);
#endif
#if CONFIG_P25_VOICE
// Copy the voice receiver's call state and counters into v. Called from the radio task's poll. True when a field changed.
bool radio_p25_voice_publish(radio_p25_view_t *v);
#endif

#endif
