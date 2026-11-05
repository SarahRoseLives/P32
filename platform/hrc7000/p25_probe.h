// SPDX-License-Identifier: MIT
// Copyright (c) 2026 The RadioFoundry Authors
// HR_C7000 P25 physical-layer receive bench probe (docs/spec/p25.md 5.1): the modem's PHY receive test mode with the
// P25 frame sync, and the raw receive RAM it leaves. CONFIG_P25_PROBE only.
#ifndef HRC_P25_PROBE_H
#define HRC_P25_PROBE_H

#include "config.h"
#include "hal_p25.h"
#include "modem/hrc7000/hrc7000.h"

#if CONFIG_P25_PROBE
void hrc_p25_probe_attach(hrc7000_t *modem);
extern const hal_p25_probe_t hrc_p25_probe;
#else
static inline void hrc_p25_probe_attach(hrc7000_t *modem) { (void)modem; }
#endif

#endif
