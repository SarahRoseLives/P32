// SPDX-License-Identifier: MIT
// Copyright (c) 2026 The RadioFoundry Authors
// The P25 PHY transmit test engine's debug command (DL_CMD_P25_TX, docs/spec/p25.md 10). It arms one explicit
// transmission of the fixed P25 test stream, starts it on the next explicit START (never on a PTT edge), stops it, or
// reports the engine's state. The ALLOW_RF token on ARM and START keeps a stray frame from keying the transmitter.
#include "config.h"

#if CONFIG_P25_TX

#include <string.h>
#include "debuglink/debuglink.h"
#include "debuglink/debuglink_proto.h"
#include "debuglink/dl_fmt.h"
#include "board.h"
#include "hal_time.h"
#include "p25/p25_tx_voice.h"
#include "p25/p25_cc_tx.h"
#include "settings/settings.h"
#include "FreeRTOS.h"
#include "task.h"
#if CONFIG_BEEP
#include "radio/beep.h"
#include "radio/radio.h"
#endif

static uint32_t get32(const uint8_t *p)
{
	return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static uint16_t get16(const uint8_t *p)
{
	return (uint16_t)(p[0] | ((uint16_t)p[1] << 8));
}

static bool token_ok(const uint8_t *p)
{
	return memcmp(p, DL_P25_TX_TOKEN, DL_P25_TX_TOKEN_LEN) == 0;
}

// The diagnostic must not fight the radio core's own transmission. The platform refuses DMR and the P25 receive
// probe separately; this covers an FM/DMR PTT the core is already serving.
static bool core_idle(void)
{
	static radio_view_t view;

	radio_get_view(&view);
	return view.state != RS_TX && view.state != RS_TX_DENIED;
}

static size_t status(uint8_t *out, size_t cap)
{
	hal_p25_tx_stats_t st;
	dl_text_t t;

	if (cap < 1) {
		return 0;
	}
	board.p25_tx->stats(&st);
	out[0] = DL_ST_OK;
	dl_text_init(&t, (char *)out + 1, cap - 1);
	dl_text(&t, "p25tx armed=%u hw=%u state=%u reason=%u hz=%u variant=%u invert=%u arm_s=%u cal=%u "
		     "offset=%u of %u blocks=%u dup=%u late=%u maxgap=%u sys=%u ts=%u rf=%u expired=%u failed=%u\n",
		(unsigned)st.armed, (unsigned)st.hardware_running, (unsigned)st.state, (unsigned)st.reason,
		(unsigned)st.frequency_hz, (unsigned)st.variant, (unsigned)st.invert, (unsigned)st.arm_remaining_s,
		(unsigned)st.calibration_valid, (unsigned)st.offset, (unsigned)st.stream_bytes, (unsigned)st.blocks,
		(unsigned)st.duplicate_irqs, (unsigned)st.late_irqs, (unsigned)st.max_gap_us, (unsigned)st.sys_irqs,
		(unsigned)st.ts_irqs, (unsigned)st.rftx_irqs, (unsigned)st.arm_expired, (unsigned)st.setup_failed);
	uint32_t mic_n, enc_n, ldus_n, drops_n;
	p25_tx_voice_stats(&mic_n, &enc_n, &ldus_n, &drops_n);
	dl_text(&t, "live=%u ldus=%u underruns=%u mic=%u enc=%u vldus=%u micdrop=%u\n",
		(unsigned)st.live, (unsigned)st.ldus, (unsigned)st.underruns,
		(unsigned)mic_n, (unsigned)enc_n, (unsigned)ldus_n, (unsigned)drops_n);
	return 1 + t.len;
}

size_t debuglink_cmd_p25_tx(const uint8_t *req, uint16_t len, uint8_t *out, size_t cap)
{
	if (board.p25_tx == NULL) {
		out[0] = DL_ST_UNAVAILABLE;
		return 1;
	}
	if (len < 1) {
		out[0] = DL_ST_BAD_ARG;
		return 1;
	}
	switch (req[0]) {
	case DL_P25_TX_OP_STATUS:
		return status(out, cap);
	case DL_P25_TX_OP_ARM: {
		if (len != 1u + 4u + 1u + 1u + DL_P25_TX_TOKEN_LEN) {
			out[0] = DL_ST_BAD_ARG;
			return 1;
		}
		if (req[6] > 1u || !token_ok(req + 7) || !core_idle()) {
			out[0] = DL_ST_REFUSED;
			return 1;
		}
		bool ok = board.p25_tx->arm(get32(req + 1), req[5], req[6] != 0, true);
		out[0] = ok ? DL_ST_OK : DL_ST_REFUSED;
		return 1;
	}
	case DL_P25_TX_OP_START:
		if (len != 1u + DL_P25_TX_TOKEN_LEN) {
			out[0] = DL_ST_BAD_ARG;
			return 1;
		}
		if (!token_ok(req + 1) || !core_idle()) {
			out[0] = DL_ST_REFUSED;
			return 1;
		}
		{
			bool started = board.p25_tx->start(hal_micros());
#if CONFIG_BEEP
			if (started) {
				radio_req_beep(BEEP_TX_PERMIT);
			}
#endif
			out[0] = started ? DL_ST_OK : DL_ST_REFUSED;
		}
		return 1;
	case DL_P25_TX_OP_LIVE_START: {
		if (len != 1u + 4u + 2u + 2u + 4u + DL_P25_TX_TOKEN_LEN) {
			out[0] = DL_ST_BAD_ARG;
			return 1;
		}
		if (!token_ok(req + 13) || !core_idle()) {
			out[0] = DL_ST_REFUSED;
			return 1;
		}
#if CONFIG_BEEP
		// Motorola-style permit: two short tones after the channel is authorized, before the microphone is live.
		radio_req_beep(BEEP_TX_PERMIT);
		vTaskDelay(pdMS_TO_TICKS(400));
#endif
		out[0] = p25_tx_voice_start(get32(req + 1), get16(req + 5), get16(req + 7), get32(req + 9), true)
			? DL_ST_OK : DL_ST_REFUSED;
		return 1;
	}
	case DL_P25_TX_OP_CC: {
		if (len != 1u + DL_P25_TX_TOKEN_LEN) {
			out[0] = DL_ST_BAD_ARG;
			return 1;
		}
		if (!token_ok(req + 1) || !core_idle()) {
			out[0] = DL_ST_REFUSED;
			return 1;
		}
		static uint8_t stream[30u * 45u];
		settings_t st;
		settings_read(&st);
		uint32_t hz = st.p25.uplink_hz;
		if (hz == 0u && st.p25.cc_hz > 5000000u) {
			hz = st.p25.cc_hz - 5000000u;
		}
		uint8_t tsbk[12];
		size_t n = 0;
		for (unsigned i = 0; i < 30u; i++) {
			if ((i & 1u) == 0u) {
				p25_tsbk_unit_registration(tsbk, st.p25.wacn, st.p25.sys, st.p25.unit_id, false, 0);
			} else {
				p25_tsbk_group_affiliation(tsbk, st.p25.sys, st.p25.tg, st.p25.unit_id);
			}
			uint8_t frame[45];
			p25_cc_build_frame(frame, st.p25.nac, tsbk);
			memcpy(&stream[n], frame, sizeof frame);
			n += sizeof frame;
		}
		if (!board.p25_tx->arm(hz, 9, false, true) ||
		    !board.p25_tx->start_raw(stream, (uint32_t)n, hal_micros())) {
			out[0] = DL_ST_REFUSED;
			return 1;
		}
		dl_text_t t;
		out[0] = DL_ST_OK;
		dl_text_init(&t, (char *)out + 1, cap - 1);
		dl_text(&t, "cc hz=%u nac=%03X wacn=%05X sys=%03X unit=%u tg=%u frames=30\n",
			(unsigned)hz, (unsigned)st.p25.nac, (unsigned)st.p25.wacn, (unsigned)st.p25.sys,
			(unsigned)st.p25.unit_id, (unsigned)st.p25.tg);
		return 1 + t.len;
	}
	case DL_P25_TX_OP_STOP:
		p25_tx_voice_stop();
		board.p25_tx->stop();
		out[0] = DL_ST_OK;
		return 1;
	default:
		out[0] = DL_ST_BAD_ARG;
		return 1;
	}
}

#endif	// CONFIG_P25_TX
