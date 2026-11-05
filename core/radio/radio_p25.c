// SPDX-License-Identifier: MIT
// Copyright (c) 2026 The RadioFoundry Authors
// The P25 control-channel monitor's view, read from the demodulator's PHY receive probe (docs/spec/p25.md 8), and the
// voice receiver's call state and counters (docs/spec/p25.md 6, 9). The probe/codec do the work; this copies it into
// the radio view.
#include "config.h"
#if CONFIG_P25 || CONFIG_P25_VOICE

#include <string.h>
#include "board.h"
#include "hal_p25.h"
#include "radio/radio_p25.h"
#if CONFIG_P25_VOICE
#include "p25/p25_voice.h"
#endif

#if CONFIG_P25
bool radio_p25_publish(radio_p25_view_t *v)
{
	hal_p25_cc_t cc;

	if (board.p25_probe == NULL || board.p25_probe->last_cc == NULL) {
		return false;
	}
	bool valid = board.p25_probe->last_cc(&cc);
	if (valid) {
		radio_p25_view_t n;

		memset(&n, 0, sizeof n);	// a fresh struct so a padding byte never reads as a change
		n.on = v->on;
		n.valid = true;
		n.opcode = cc.opcode;
		n.nac = cc.nac;
		n.duid = cc.duid;
		n.wacn = cc.wacn;
		n.sys = cc.sys;
		n.rfss = cc.rfss;
		n.site = cc.site;
		n.tg = cc.tg;
		n.vch = cc.vch;
		n.freq_hz = cc.freq_hz;
		n.frames = cc.blocks;
		if (memcmp(&n, v, sizeof n) != 0) {
			*v = n;
			return true;
		}
	}
	return false;
}
#endif

#if CONFIG_P25_VOICE
bool radio_p25_voice_publish(radio_p25_view_t *v)
{
	p25_voice_stats_t st;
	radio_p25_view_t n;

	p25_voice_stats(&st);
	memset(&n, 0, sizeof n);
	n.on = v->on;
	n.voice = true;
	n.valid = st.units != 0;
	n.call_state = st.state;
	n.call_nac = st.nac;
	n.call_group = st.group;
	n.units = st.units;
	n.voices = st.voices;
	n.underruns = st.underruns;
	n.overflows = st.overflows;
	if (memcmp(&n, v, sizeof n) != 0) {
		*v = n;
		return true;
	}
	return false;
}
#endif

#endif
