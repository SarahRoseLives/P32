// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 The RadioFoundry Authors
// Continuous P25 control-channel receiver: the FM discriminator through the software demodulator, so a short NID
// defect does not stop the monitor (docs/spec/p25.md 8). RX only.
#ifndef RFOUNDRY_P25_CC_RX_H
#define RFOUNDRY_P25_CC_RX_H

#include <stdbool.h>
#include "config.h"
#include "hal_p25.h"

#if CONFIG_P25
bool p25_cc_rx_start(void);
void p25_cc_rx_stop(void);
bool p25_cc_rx_running(void);
// Copy the latest decoded control channel. Returns true once a control-channel NID has been decoded since start.
bool p25_cc_rx_last(hal_p25_cc_t *out);
#endif

#endif
