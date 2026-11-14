// SPDX-License-Identifier: MIT
// Copyright (c) 2026 The RadioFoundry Authors
// DL_CMD_P25_SYS (docs/spec/debuglink.md 3.24): the P25 trunking systems in the settings store, read and written whole.
// The systems are settings, not codeplug records (docs/spec/p25.md 8.1), so the web codeplug editor needs this to
// configure them beside the channels that name them.
#include <string.h>
#include "config.h"
#include "hal_time.h"
#include "debuglink/debuglink.h"
#include "debuglink/debuglink_proto.h"
#include "settings/settings.h"

#if CONFIG_P25

static uint16_t get16(const uint8_t *p)
{
	return (uint16_t)((uint16_t)p[0] | (uint16_t)p[1] << 8);
}

static uint32_t get32(const uint8_t *p)
{
	return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}

static void put16(uint8_t *p, uint16_t v)
{
	p[0] = (uint8_t)v;
	p[1] = (uint8_t)(v >> 8);
}

static void put32(uint8_t *p, uint32_t v)
{
	p[0] = (uint8_t)v;
	p[1] = (uint8_t)(v >> 8);
	p[2] = (uint8_t)(v >> 16);
	p[3] = (uint8_t)(v >> 24);
}

static void put_record(uint8_t *p, const settings_p25_t *s)
{
	put32(p + 0, s->cc_hz);
	put32(p + 4, s->uplink_hz);
	put32(p + 8, s->wacn);
	put32(p + 12, s->unit_id);
	put32(p + 16, s->base_hz);
	put32(p + 20, s->spacing_hz);
	put32(p + 24, (uint32_t)s->offset_hz);
	put16(p + 28, s->sys);
	put16(p + 30, s->nac);
	put16(p + 32, s->tg);
	put16(p + 34, s->rfss);
	put16(p + 36, s->site);
	p[38] = s->band_auto ? 1 : 0;
	p[39] = s->valid ? 1 : 0;
	memset(p + 40, 0, DL_P25_SYS_NAME);
	size_t n = strnlen(s->name, DL_P25_SYS_NAME);
	memcpy(p + 40, s->name, n);
}

// false: a field outside what the firmware stores (the record is not taken at all)
static bool get_record(const uint8_t *p, settings_p25_t *s)
{
	memset(s, 0, sizeof(*s));
	s->cc_hz = get32(p + 0);
	s->uplink_hz = get32(p + 4);
	s->wacn = get32(p + 8);
	s->unit_id = get32(p + 12);
	s->base_hz = get32(p + 16);
	s->spacing_hz = get32(p + 20);
	s->offset_hz = (int32_t)get32(p + 24);
	s->sys = get16(p + 28);
	s->nac = get16(p + 30);
	s->tg = get16(p + 32);
	s->rfss = get16(p + 34);
	s->site = get16(p + 36);
	s->band_auto = p[38] ? 1 : 0;
	s->valid = p[39] ? 1 : 0;
	memcpy(s->name, p + 40, DL_P25_SYS_NAME);
	s->name[DL_P25_SYS_NAME] = 0;
	return s->wacn <= 0xFFFFFu && s->sys <= 0xFFFu && s->nac <= 0xFFFu;
}

size_t debuglink_cmd_p25_sys(const uint8_t *req, uint16_t len, uint8_t *out, size_t cap)
{
	if (len < 1) {
		out[0] = DL_ST_BAD_ARG;
		return 1;
	}
	settings_t *st = settings_begin();
	if (req[0] == DL_P25_SYS_OP_GET) {
		uint8_t count = st->p25_count;
		if (count == 0 || count > SETTINGS_P25_SYSTEMS) {
			count = 1;
		}
		if (len != 1 || 3u + (size_t)count * DL_P25_SYS_RECORD > cap) {
			settings_end(hal_millis());
			out[0] = DL_ST_BAD_ARG;
			return 1;
		}
		out[0] = DL_ST_OK;
		out[1] = count;
		out[2] = st->p25_active < count ? st->p25_active : 0;
		for (uint8_t i = 0; i < count; i++) {
			put_record(out + 3 + (size_t)i * DL_P25_SYS_RECORD, settings_p25_get(st, i));
		}
		settings_end(hal_millis());
		return 3u + (size_t)count * DL_P25_SYS_RECORD;
	}
	if (req[0] != DL_P25_SYS_OP_SET || len < 3) {
		settings_end(hal_millis());
		out[0] = DL_ST_BAD_ARG;
		return 1;
	}
	uint8_t count = req[1];
	uint8_t active = req[2];
	if (count == 0 || count > SETTINGS_P25_SYSTEMS || active >= count ||
	    len != 3u + (size_t)count * DL_P25_SYS_RECORD) {
		settings_end(hal_millis());
		out[0] = DL_ST_BAD_ARG;
		return 1;
	}
	settings_p25_t tmp[SETTINGS_P25_SYSTEMS];
	for (uint8_t i = 0; i < count; i++) {
		if (!get_record(req + 3 + (size_t)i * DL_P25_SYS_RECORD, &tmp[i])) {
			settings_end(hal_millis());
			out[0] = DL_ST_BAD_ARG;
			return 1;
		}
	}
	for (uint8_t i = 0; i < count; i++) {
		*settings_p25_mut(st, i) = tmp[i];
	}
	st->p25_count = count;
	st->p25_active = active;
	settings_end(hal_millis());
	out[0] = DL_ST_OK;
	return 1;
}

#else

size_t debuglink_cmd_p25_sys(const uint8_t *req, uint16_t len, uint8_t *out, size_t cap)
{
	(void)req;
	(void)len;
	(void)cap;
	out[0] = DL_ST_UNAVAILABLE;
	return 1;
}

#endif
