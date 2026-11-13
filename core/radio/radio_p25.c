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
#if CONFIG_P25
#include "p25/p25_cc_rx.h"
#endif
#if CONFIG_P25_VOICE
#include "p25/p25_voice.h"
#endif

#if CONFIG_P25
bool radio_p25_publish(radio_p25_view_t *v)
{
	hal_p25_cc_t cc;

	bool valid = p25_cc_rx_last(&cc);
	if (!valid) {
		// The carrier can be present while the NID is unreadable. Publish that activity, but keep the last decoded
		// fields so the UI can say "no control channel" instead of pretending the radio is out of range.
		if (v->activity != cc.activity) {
			v->activity = cc.activity;
			return true;
		}
		return false;
	}
	{
		radio_p25_view_t n;

		memset(&n, 0, sizeof n);	// a fresh struct so a padding byte never reads as a change
		n.on = v->on;
		n.valid = true;
		n.out_of_range = v->out_of_range;
		n.no_control = v->no_control;
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
		n.activity = cc.activity;
		n.voice_hz = cc.voice_hz;
		n.voice_tx_hz = cc.voice_tx_hz;
		n.voice_id = cc.voice_id;
		n.voice_grant = cc.voice_grant;
		n.source = cc.source;
		n.grants = cc.grants;
		n.affiliated = v->affiliated || cc.affiliated;
		n.affiliate_tg = cc.affiliate_tg;
		n.affiliate_source = cc.affiliate_source;
		n.affiliation_seq = cc.affiliation_seq;
		n.system_tg = v->system_tg;
		n.system_unit = v->system_unit;
		n.system_cc_hz = v->system_cc_hz;
		n.system_uplink_hz = v->system_uplink_hz;
		n.system_voice_hz = v->system_voice_hz;
		n.system_nac = v->system_nac;
		n.system_sys = v->system_sys;
		n.system_wacn = v->system_wacn;
		n.follow = v->follow;
		n.voice = v->voice;
		n.conv = v->conv;
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
	n.conv = v->conv;
	n.valid = st.units != 0;
	n.out_of_range = v->out_of_range;
	n.no_control = v->no_control;
	n.activity = v->activity;
	n.call_state = st.state;
	n.call_nac = st.nac;
	n.call_group = st.group;
	n.call_source = st.source;
	n.units = st.units;
	n.voices = st.voices;
	n.underruns = st.underruns;
	n.overflows = st.overflows;
	n.repeated = st.repeated;
	n.muted = st.muted;
	n.resets = st.resets;
	n.drops = st.drops;
	n.peak = st.peak;
	n.corrected = st.corrected;
	n.follow = v->follow;
	n.voice_hz = v->voice_hz;
	n.affiliated = v->affiliated;
	n.affiliate_tg = v->affiliate_tg;
	n.affiliate_source = v->affiliate_source;
	n.affiliation_seq = v->affiliation_seq;
	n.system_tg = v->system_tg;
	n.system_unit = v->system_unit;
	n.system_cc_hz = v->system_cc_hz;
	n.system_uplink_hz = v->system_uplink_hz;
	n.system_voice_hz = v->system_voice_hz;
	n.system_nac = v->system_nac;
	n.system_sys = v->system_sys;
	n.system_wacn = v->system_wacn;
	n.boost = p25_voice_agc();
	if (memcmp(&n, v, sizeof n) != 0) {
		*v = n;
		return true;
	}
	return false;
}
#endif

#endif
