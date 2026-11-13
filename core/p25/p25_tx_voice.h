// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 The RadioFoundry Authors
// Conventional P25 Phase 1 voice transmit: microphone PCM -> IMBE -> LDU stream through the platform's P25 PHY
// transmitter (docs/spec/p25.md 10). It runs in its own task; the radio core owns PTT and authorization.
#ifndef RFOUNDRY_P25_TX_VOICE_H
#define RFOUNDRY_P25_TX_VOICE_H

#include <stdint.h>
#include <stdbool.h>
#include "config.h"
#include "settings/settings.h"

#if CONFIG_P25_TX
#ifdef __cplusplus
extern "C" {
#endif
// Start one clear group call on hz. allow_rf must be true; the platform checks the band, calibration and battery.
// The RF path starts only after two complete LDUs are ready, so the microphone never underruns the preamble.
bool p25_tx_voice_start(uint32_t hz, uint16_t nac, uint16_t tg, uint32_t source, bool allow_rf);
// Release: stop the microphone, pad the current LDU with silence, finish the stream and stop the RF path.
void p25_tx_voice_stop(void);
// True from start until the last TDU has drained and the RF path is restored.
bool p25_tx_voice_active(void);
// Diagnostics: microphone blocks queued, IMBE frames encoded, LDUs published, and blocks dropped.
void p25_tx_voice_stats(uint32_t *captured, uint32_t *encoded, uint32_t *ldus, uint32_t *drops);
// Send a fixed control-channel burst (alternating Unit Registration and Group Affiliation) on the system's uplink.
// This is the radio-local equivalent of the debug CC op, for bench captures. Returns false if the PHY refused.
bool p25_cc_burst_start(const settings_p25_t *p25, uint16_t frames);
// PTT: register and affiliate on the control-channel uplink. One repeating burst at channel selection gives the
// site's lossy decoder enough frames; the radio then returns to the control channel without retrying. False if the
// PHY refused.
bool p25_cc_affiliate_start(const settings_p25_t *p25);
// PTT: a short group voice request burst, then the caller listens for the grant. Short on purpose: the site queues
// the grant immediately, and a long request would finish with the grant already gone.
bool p25_cc_voice_request_start(const settings_p25_t *p25);
// Power-off: Unit De-Registration on the control-channel uplink, repeated for the site's lossy decoder.
bool p25_cc_deregister_start(const settings_p25_t *p25);
#ifdef __cplusplus
}
#endif
#endif

#endif
