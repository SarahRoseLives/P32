// SPDX-License-Identifier: MIT
// Copyright (c) 2026 The RadioFoundry Authors
// HR_C7000 P25 physical-layer transmit test engine (docs/spec/p25.md 10): the modem's PHY transmitter with the fixed
// P25 test stream. CONFIG_P25_TX. It borrows the board's DMR RF/PA hooks without running the DMR engine.
#ifndef HRC_P25_TX_H
#define HRC_P25_TX_H

#include "config.h"
#include "hal_p25_tx.h"

#if CONFIG_P25_TX
extern const hal_p25_tx_t hrc_p25_tx;
#endif

#endif
