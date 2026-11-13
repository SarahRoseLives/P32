// SPDX-License-Identifier: MIT
// Copyright (c) 2026 The RadioFoundry Authors
// Optional large-panel Home: one channel instrument for analog FM, conventional P25 and trunked P25. The frame stays
// put; RX, TX and the channel family change its contents. Smaller and monochrome panels keep the standard Home.
#include "config.h"
#include "ui_instrument.h"
#if CONFIG_UI_INSTRUMENT && (CONFIG_P25 || CONFIG_P25_VOICE)

#include <string.h>
#include "board.h"
#include "fmt.h"
#include "i18n.h"
#include "ui_screens.h"

#define ACCENT_W	4
#define CONTENT_X	14
#define CONTENT_R	10
#define RAIL_Y		5
#define STATE_GAP	5

static int rssi_bars(int16_t dbm)
{
	if (dbm >= -88) return 4;
	if (dbm >= -98) return 3;
	if (dbm >= -108) return 2;
	if (dbm >= -118) return 1;
	return 0;
}

static int view_bars(const radio_view_t *v)
{
	return v->ch.p25 && (v->p25.out_of_range || v->p25.no_control) ? 0 : rssi_bars(v->rssi_dbm);
}

static void hex(sbuf_t *b, uint32_t v, int digits)
{
	static const char d[] = "0123456789ABCDEF";
	char t[9];
	int n = 0;
	if (digits > 8) digits = 8;
	do {
		t[n++] = d[v & 15u];
		v >>= 4;
	} while (v != 0 && n < digits);
	while (n < digits) t[n++] = '0';
	while (n > 0) sb_chr(b, t[--n]);
}

static bool call_active(const radio_view_t *v)
{
	return v->p25.voice && v->p25.call_state != 0;
}

static radio_state_t visible_state(const radio_view_t *v)
{
	if (v->state == RS_TX || v->state == RS_TX_DENIED) return v->state;
	if (call_active(v)) return RS_RX;
	return v->state;
}

static const char *state_word(radio_state_t st)
{
	switch (st) {
	case RS_TX: return "TX";
	case RS_TX_DENIED: return "NO TX";
	case RS_RX: return "RX";
	default: return "IDLE";
	}
}

static color_t state_color(radio_state_t st)
{
	switch (st) {
	case RS_TX: return ui_col.tx;
	case RS_TX_DENIED: return ui_col.warn;
	case RS_RX: return ui_col.rx;
	default: return ui_col.idle;
	}
}

static bool conv(const radio_view_t *v) { return v->p25.conv; }
static bool trunk(const radio_view_t *v) { return v->p25.on && !v->p25.conv; }
static bool p25(const radio_view_t *v) { return conv(v) || trunk(v) || v->p25.voice; }

static color_t accent(const radio_view_t *v)
{
	if (v->state == RS_TX) return ui_col.tx;
	if (call_active(v) || v->state == RS_RX) return ui_col.rx;
	if (trunk(v)) return ui_col.priv;
	if (conv(v)) return ui_col.focus;
	return ui_col.rx;
}

static const char *mode_word(const radio_view_t *v)
{
	if (v->state == RS_TX) return conv(v) ? "P25 CNV" : trunk(v) ? "P25 TRK" : "FM";
	if (trunk(v)) return "P25 TRK";
	if (conv(v) || v->p25.voice) return "P25 CNV";
	return "FM";
}

static void draw_rail(const radio_view_t *v, radio_state_t st)
{
	const ui_layout_t *L = &ui_layout;
	int w = gfx_width(), y = RAIL_Y;
	int th = L->tiny->h;
	int sw = gfx_text_width(L->tiny, state_word(st)) + 7;
	int x = CONTENT_X;

	ui_strip_draws++;
	gfx_fill(CONTENT_X, y, sw, th + 2, state_color(st));
	gfx_text(L->tiny, x + 3, y + 1, state_word(st), ui_col.pill_fg);
	x += sw + STATE_GAP;
	gfx_text(L->tiny, x, y + 1, mode_word(v), accent(v));

	int right = w - CONTENT_R;
	int bat_w = 18, bat_h = th - 4;
	uint8_t pct = v->battery_pct > 100u ? 100u : v->battery_pct;
	gfx_icon_battery(right - bat_w, y + 2, bat_w, bat_h, pct, ui_col.dim, ui_col.fg);
	right -= bat_w + 5;
	int sig_w = 12;
	gfx_icon_bars(right - sig_w, y + 3, sig_w, th - 5, 4, view_bars(v), ui_col.fg, ui_col.line);
}

static void draw_eyebrow(const radio_view_t *v)
{
	const ui_layout_t *L = &ui_layout;
	int w = gfx_width();
	int y = RAIL_Y + L->tiny->h + 9;
	char left[48], right[24], fit[48];
	sbuf_t b;

	sb_init(&b, left, sizeof left);
	if (v->vfo) {
		sb_str(&b, "VFO");
	} else {
		sb_str(&b, v->zone_name);
		sb_str(&b, "  /  ");
		sb_str(&b, v->ch.name);
	}
	sb_init(&b, right, sizeof right);
	if (v->vfo) {
		sb_str(&b, TR("STEP "));
		sb_u32(&b, v->vfo_step_hz / 1000u, 1);
		sb_chr(&b, 'k');
	} else {
		sb_str(&b, "CH ");
		sb_u32(&b, v->channel_number, 1);
	}
	int rw = gfx_text_width(L->tiny, right);
	gfx_text(L->tiny, w - CONTENT_R - rw, y, right, ui_col.dim);
	gfx_text_fit(L->tiny, left, w - CONTENT_R - rw - L->tiny->w, fit, sizeof fit);
	gfx_text(L->tiny, CONTENT_X, y, fit, ui_col.fg);
	gfx_hline(CONTENT_X, y + L->tiny->h + 3, w - CONTENT_X - CONTENT_R, ui_col.line);
}

static const char *call_label(uint8_t state)
{
	switch (state) {
	case 1: return TR("CLEAR VOICE");
	case 2: return TR("ENCRYPTED");
	case 3: return TR("UNSUPPORTED");
	case 4: return TR("BAD SIGNAL");
	default: return TR("WAIT FOR CALL");
	}
}

static void draw_display(const radio_view_t *v, radio_state_t st, const ui_state_t *s, int *sub_y)
{
	const ui_layout_t *L = &ui_layout;
	int w = gfx_width();
	// Starts below the eyebrow rule, which is at RAIL_Y + 2 * tiny.h + 12.
	int y = RAIL_Y + 2 * L->tiny->h + 18;
	char main[48], label[32], sub[72];
	sbuf_t b;
	const font_t *mf = L->large;
	color_t mc = ui_col.fg;
	bool freq = false;

	sb_init(&b, main, sizeof main);
	if (s->entry) {
		int dec = s->entry_len > UI_ENTRY_MHZ ? s->entry_len - UI_ENTRY_MHZ : 0;
		int show = dec < 3 ? 3 : dec;
		for (int i = 0; i < UI_ENTRY_MHZ + show; i++) {
			if (i == UI_ENTRY_MHZ)
				sb_chr(&b, '.');
			sb_chr(&b, i < s->entry_len ? s->entry_digits[i] : '_');
		}
		freq = true;
	} else if (st == RS_TX) {
		sb_mmss(&b, v->tx_elapsed_ms / 1000u);
	} else if (call_active(v)) {
		if (v->p25.call_source != 0) {
			sb_str(&b, "UNIT ");
			sb_u32(&b, v->p25.call_source, 1);
		} else {
			sb_str(&b, TR("CALL"));
		}
	} else if (trunk(v)) {
		sb_str(&b, "TG ");
		sb_u32(&b, v->p25.system_tg, 1);
	} else if (conv(v)) {
		sb_freq(&b, v->ch.rx_hz);
		freq = true;
	} else {
		sb_freq(&b, v->ch.rx_hz);
		freq = true;
	}

	if (s->entry) {
		str_copy(label, sizeof label, TR("VFO FREQUENCY"));
	} else if (st == RS_TX) {
		str_copy(label, sizeof label, TR("TRANSMITTING"));
	} else if (call_active(v)) {
		str_copy(label, sizeof label, conv(v) ? TR("CONVENTIONAL CALL") : TR("TRUNKED CALL"));
	} else if (trunk(v)) {
		str_copy(label, sizeof label, TR("P25 TRUNKED"));
	} else if (conv(v)) {
		str_copy(label, sizeof label, TR("P25 CONVENTIONAL"));
	} else {
		str_copy(label, sizeof label, TR("ANALOG FM"));
	}

	sb_init(&b, sub, sizeof sub);
	if (s->entry) {
		sb_str(&b, TR("OK TUNE  BACK CANCEL"));
	} else if (st == RS_TX) {
		if (v->p25.voice || conv(v) || trunk(v)) {
			sb_str(&b, TR("TG "));
			sb_u32(&b, v->p25.system_tg, 1);
			sb_str(&b, "  ");
			sb_str(&b, TR("UID "));
			sb_u32(&b, v->p25.system_unit, 1);
		} else {
			sb_str(&b, TR("TONE "));
			sb_tone(&b, &v->ch.tx_tone);
			sb_str(&b, "  ");
			sb_str(&b, TR("POWER "));
			sb_str(&b, ui_power_name(v->power));
		}
	} else if (call_active(v)) {
		sb_str(&b, TR("TG "));
		sb_u32(&b, v->p25.call_group, 1);
		sb_str(&b, "  ");
		sb_str(&b, TR("NAC "));
		hex(&b, v->p25.call_nac, 3);
	} else if (trunk(v)) {
		sb_str(&b, TR("SYS "));
		hex(&b, v->p25.system_sys, 3);
		sb_str(&b, "  ");
		sb_str(&b, TR("CC "));
		sb_freq(&b, v->p25.system_cc_hz);
	} else if (conv(v)) {
		sb_str(&b, TR("TG "));
		sb_u32(&b, v->p25.system_tg, 1);
		sb_str(&b, "  ");
		sb_str(&b, TR("NAC "));
		hex(&b, v->p25.system_nac, 3);
		sb_str(&b, "  ");
		sb_str(&b, TR("UID "));
		sb_u32(&b, v->p25.system_unit, 1);
	} else {
		sb_str(&b, TR("TONE "));
		sb_tone(&b, &v->ch.rx_tone);
		sb_str(&b, "  ");
		sb_str(&b, TR("POWER "));
		sb_str(&b, ui_power_name(v->power));
	}

	if (trunk(v) && (v->p25.out_of_range || v->p25.no_control)) {
		str_copy(main, sizeof main, TR("OUT OF RANGE"));
		str_copy(label, sizeof label, TR("P25 TRUNKED"));
		str_copy(sub, sizeof sub, TR("NO CONTROL CHANNEL"));
		freq = false;
		mc = ui_col.warn;
	}
	if (st == RS_TX) mc = ui_col.tx;
	else if (call_active(v)) mc = ui_col.rx;

	gfx_text(L->tiny, CONTENT_X, y, label, mc);
	y += L->tiny->h + 3;
	if (gfx_text_width(mf, main) > w - CONTENT_X - CONTENT_R) mf = L->medium;
	if (gfx_text_width(mf, main) > w - CONTENT_X - CONTENT_R) mf = L->small;
	gfx_text(mf, CONTENT_X, y, main, mc);
	if (freq) {
		int x = CONTENT_X + gfx_text_width(mf, main) + 4;
		if (x + gfx_text_width(L->tiny, "MHz") <= w - CONTENT_R)
			gfx_text(L->tiny, x, y + mf->h - L->tiny->h, "MHz", ui_col.dim);
	}
	y += mf->h + 4;
	gfx_text_fit(L->small, sub, w - CONTENT_X - CONTENT_R, sub, sizeof sub);
	gfx_text(L->small, CONTENT_X, y, sub, ui_col.fg);
	*sub_y = y + L->small->h;
}

static void draw_activity(const radio_view_t *v, radio_state_t st, int y)
{
	const ui_layout_t *L = &ui_layout;
	int w = gfx_width();
	bool oos = trunk(v) && (v->p25.out_of_range || v->p25.no_control);
	const char *word = oos ? TR("OUT OF RANGE") : st == RS_TX ? TR("ON AIR") : call_active(v) ? TR("RECEIVING") :
		st == RS_RX ? TR("CARRIER") : TR("READY");
	const char *right;
	char rbuf[24], lbuf[48];
	sbuf_t b;

	if (oos) {
		right = "";	// the large word and the line below already say why; a long right label would overlap it
	} else if (st == RS_TX) {
		sb_init(&b, rbuf, sizeof rbuf);
		sb_str(&b, TR("RELEASE PTT"));
		right = rbuf;
	} else if (call_active(v)) {
		sb_init(&b, rbuf, sizeof rbuf);
		sb_i32(&b, v->rssi_dbm);
		sb_str(&b, " dBm");
		right = rbuf;
	} else {
		sb_init(&b, rbuf, sizeof rbuf);
		sb_str(&b, TR("RSSI "));
		sb_i32(&b, v->rssi_dbm);
		right = rbuf;
	}
	sb_init(&b, lbuf, sizeof lbuf);
	sb_str(&b, word);
	if (st == RS_TX) {
		sb_str(&b, "  ");
		sb_str(&b, ui_power_name(v->power));
	} else if (call_active(v) && v->p25.call_state != 1) {
		sb_str(&b, "  ");
		sb_str(&b, call_label(v->p25.call_state));
	}

	gfx_hline(CONTENT_X, y, w - CONTENT_X - CONTENT_R, ui_col.line);
	y += 5;
	gfx_fill(CONTENT_X, y + 3, 4, 4, st == RS_TX ? ui_col.tx : state_color(st));
	gfx_text(L->tiny, CONTENT_X + 8, y, lbuf, oos ? ui_col.warn : st == RS_TX ? ui_col.tx : ui_col.fg);
	int rw = gfx_text_width(L->tiny, right);
	gfx_text(L->tiny, w - CONTENT_R - rw, y, right, ui_col.dim);
	gfx_hline(CONTENT_X, y + L->tiny->h + 3, w - CONTENT_X - CONTENT_R, ui_col.line);
}

static void draw_meter(const radio_view_t *v, radio_state_t st, int y)
{
	if (st != RS_TX && !call_active(v) && st != RS_RX) return;
	int w = gfx_width(), n = 13, bw = 3, gap = 2;
	int x0 = CONTENT_X;
	color_t c = st == RS_TX ? ui_col.tx : ui_col.rx;
	int lit = st == RS_TX ? 9 : view_bars(v) * 3;
	if (call_active(v) && v->p25.peak != 0) {
		lit = ((unsigned)v->p25.peak * (unsigned)n + 16383u) / 32768u;
		if (lit > n) lit = n;
	}
	for (int i = 0; i < n; i++) {
		// One rising ramp, not a repeating five-bar ornament: the lit count carries the level.
		int hh = 3 + (i * 12) / (n - 1);
		gfx_fill(x0 + i * (bw + gap), y + 18 - hh, bw, hh, i < lit ? c : ui_col.line);
	}
	(void)w;
}

static void draw_deck(const radio_view_t *v, radio_state_t st, int y)
{
	const ui_layout_t *L = &ui_layout;
	int w = gfx_width(), col = (w - CONTENT_X - CONTENT_R) / 3;
	char l1[8], l2[8], l3[8], v1[18], v2[18], v3[18];
	sbuf_t b;

	if (call_active(v)) {
		str_copy(l1, sizeof l1, "NAC");
		str_copy(l2, sizeof l2, "TG");
		str_copy(l3, sizeof l3, "STATE");
		sb_init(&b, v1, sizeof v1); hex(&b, v->p25.call_nac, 3);
		sb_init(&b, v2, sizeof v2); sb_u32(&b, v->p25.call_group, 1);
		sb_init(&b, v3, sizeof v3); sb_str(&b, call_label(v->p25.call_state));
	} else if (trunk(v)) {
		str_copy(l1, sizeof l1, "CONTROL");
		str_copy(l2, sizeof l2, "SYSTEM");
		str_copy(l3, sizeof l3, "UNIT");
		sb_init(&b, v1, sizeof v1); sb_freq(&b, v->p25.system_cc_hz);
		sb_init(&b, v2, sizeof v2); hex(&b, v->p25.system_sys, 3);
		sb_init(&b, v3, sizeof v3); sb_u32(&b, v->p25.system_unit, 1);
	} else if (conv(v)) {
		str_copy(l1, sizeof l1, "NAC");
		str_copy(l2, sizeof l2, "TG");
		str_copy(l3, sizeof l3, "UID");
		sb_init(&b, v1, sizeof v1); hex(&b, v->p25.system_nac, 3);
		sb_init(&b, v2, sizeof v2); sb_u32(&b, v->p25.system_tg, 1);
		sb_init(&b, v3, sizeof v3); sb_u32(&b, v->p25.system_unit, 1);
	} else {
		str_copy(l1, sizeof l1, "TONE");
		str_copy(l2, sizeof l2, "BAND");
		str_copy(l3, sizeof l3, "POWER");
		sb_init(&b, v1, sizeof v1); sb_tone(&b, &v->ch.rx_tone);
		sb_init(&b, v2, sizeof v2); sb_str(&b, v->ch.bw == RADIO_BW_25K ? "25 kHz" : "12.5 kHz");
		sb_init(&b, v3, sizeof v3); sb_str(&b, ui_power_name(v->power));
	}
	if (st == RS_TX) {
		sb_init(&b, v3, sizeof v3);
		sb_str(&b, ui_power_name(v->power));
	}

	const char *labs[3] = { l1, l2, l3 };
	const char *vals[3] = { v1, v2, v3 };
	gfx_hline(CONTENT_X, y, w - CONTENT_X - CONTENT_R, ui_col.line);
	for (int i = 0; i < 3; i++) {
		int x = CONTENT_X + i * col;
		char fit[18];
		gfx_text(L->tiny, x, y + 5, labs[i], ui_col.dim);
		gfx_text_fit(L->small, vals[i], col - 4, fit, sizeof fit);
		gfx_text(L->small, x, y + 5 + L->tiny->h + 2, fit, ui_col.fg);
	}
}

bool ui_instrument_home(const radio_view_t *v, const ui_state_t *s)
{
	int w = gfx_width(), h = gfx_height();
	if (w < 220 || h < 300 || gfx_bpp() == 1 || s->large_text) return false;
	if (CONFIG_DMR && v->ch.mode == RADIO_MODE_DMR) return false;
	if (!p25(v) && CONFIG_DMR && v->ch.mode != RADIO_MODE_FM) return false;

	radio_state_t st = visible_state(v);
	int sub_y = 0;
	int hint = ui_layout.hint_y;
	int deck_y = hint - (ui_layout.tiny->h + ui_layout.small->h + 13);
	int act_y = deck_y - (ui_layout.tiny->h + 14);

	gfx_clear(ui_col.bg);
	gfx_fill(0, 0, ACCENT_W, h, accent(v));
	draw_rail(v, st);
	draw_eyebrow(v);
	draw_display(v, st, s, &sub_y);
	draw_meter(v, st, sub_y > act_y - 24 ? sub_y : act_y - 24);
	draw_activity(v, st, act_y);
	draw_deck(v, st, deck_y);
	ui_draw_banner(s);
	const char *hint_text;
	if (st == RS_TX) {
		hint_text = TR("Release PTT");
	} else if (call_active(v)) {
		hint_text = TR("PTT Reply  BACK");
	} else if (s->entry) {
		hint_text = TR("OK TUNE  BACK CANCEL");
	} else if (v->vfo) {
		hint_text = TR("TYPE FREQ  HOLD OK PROG");
	} else {
		hint_text = TR("OK Menu  * Quick");
	}
	ui_draw_bottom(s, hint_text);
	return true;
}

#endif
