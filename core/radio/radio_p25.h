// SPDX-License-Identifier: MIT
// Copyright (c) 2026 The RadioFoundry Authors
// The P25 control-channel monitor's view: the radio task reads the probe's last decoded control channel into the
// radio view while the monitor screen listens (docs/spec/p25.md 8). CONFIG_P25.
#ifndef RADIO_P25_H
#define RADIO_P25_H

#include "config.h"
#include "radio/radio.h"

#if CONFIG_P25
// Read the probe's last decoded control channel into v (its on flag is left as the state machine set it). Called from
// the radio task's poll; runs the decode in that task. Returns true when a shown field changed.
bool radio_p25_publish(radio_p25_view_t *v);
#endif

#endif
