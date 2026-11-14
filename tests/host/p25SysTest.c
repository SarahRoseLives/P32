// SPDX-License-Identifier: MIT
// Copyright (c) 2026 The RadioFoundry Authors
// DL_CMD_P25_SYS (core/debuglink/debuglink_p25_sys.c, docs/spec/debuglink.md 3.24): the settings systems read and
// written whole, and the records the wire format promises.
#include <assert.h>
#include <stdint.h>
#include <string.h>
#include "settings/settings.h"
#include "debuglink/debuglink_proto.h"

size_t debuglink_cmd_p25_sys(const uint8_t *req, uint16_t len, uint8_t *out, size_t cap);

static settings_t fake;

settings_t *settings_begin(void) { return &fake; }
void settings_end(uint32_t now_ms) { (void)now_ms; }
uint32_t hal_millis(void) { return 0; }

static uint16_t get16(const uint8_t *p) { return (uint16_t)(p[0] | p[1] << 8); }
static uint32_t get32(const uint8_t *p) { return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24; }

int main(void)
{
	memset(&fake, 0, sizeof fake);
	settings_p25_t *a = settings_p25_mut(&fake, 0);
	settings_p25_t *b = settings_p25_mut(&fake, 1);
	memset(a, 0, sizeof *a);
	memset(b, 0, sizeof *b);
	a->cc_hz = 438800000; a->uplink_hz = 433800000; a->wacn = 0xBEEF0; a->sys = 0x123; a->nac = 0x293;
	a->tg = 10; a->rfss = 1; a->site = 2; a->unit_id = 3144613; a->band_auto = 1; a->valid = 1; strcpy(a->name, "PARROT");
	b->cc_hz = 439000000; b->uplink_hz = 434000000; b->wacn = 0x12345; b->sys = 0x456; b->nac = 0xABC;
	b->tg = 1; b->rfss = 3; b->site = 4; b->unit_id = 42; b->band_auto = 0; b->valid = 1; strcpy(b->name, "SITE 2");
	fake.p25_count = 2;
	fake.p25_active = 1;

	uint8_t out[1024];
	const uint8_t get[] = { DL_P25_SYS_OP_GET };
	size_t n = debuglink_cmd_p25_sys(get, sizeof get, out, sizeof out);
	assert(n == 3 + 2 * DL_P25_SYS_RECORD);
	assert(out[0] == 0 && out[1] == 2 && out[2] == 1);
	assert(get32(out + 3 + 0) == 438800000);
	assert(get32(out + 3 + 4) == 433800000);
	assert(get32(out + 3 + 8) == 0xBEEF0);
	assert(get32(out + 3 + 12) == 3144613);
	assert(get16(out + 3 + 28) == 0x123);
	assert(get16(out + 3 + 30) == 0x293);
	assert(get16(out + 3 + 32) == 10);
	assert(out[3 + 38] == 1 && out[3 + 39] == 1);
	assert(strcmp((const char *)out + 3 + 40, "PARROT") == 0);
	assert(get32(out + 3 + DL_P25_SYS_RECORD) == 439000000);
	assert(out[3 + DL_P25_SYS_RECORD + 38] == 0);
	assert(strcmp((const char *)out + 3 + DL_P25_SYS_RECORD + 40, "SITE 2") == 0);

	// A SET replaces the systems and the selected slot; a later GET reads them back
	uint8_t set[3 + 2 * DL_P25_SYS_RECORD] = { DL_P25_SYS_OP_SET, 2, 0 };
	memcpy(set + 3, out + 3, 2 * DL_P25_SYS_RECORD);
	assert(debuglink_cmd_p25_sys(set, sizeof set, out, sizeof out) == 1 && out[0] == 0);
	assert(fake.p25_count == 2 && fake.p25_active == 0);
	assert(settings_p25_get(&fake, 0)->nac == 0x293 && settings_p25_get(&fake, 1)->tg == 1);
	assert(strcmp(settings_p25_get(&fake, 1)->name, "SITE 2") == 0);

	// Broken requests are refused whole
	set[1] = 0;
	assert(debuglink_cmd_p25_sys(set, 1 + 3, out, sizeof out) == 1 && out[0] == DL_ST_BAD_ARG);
	set[1] = 2;
	set[2] = 2;
	assert(debuglink_cmd_p25_sys(set, sizeof set, out, sizeof out) == 1 && out[0] == DL_ST_BAD_ARG);
	set[2] = 0;
	uint8_t bad[3 + DL_P25_SYS_RECORD] = { DL_P25_SYS_OP_SET, 1, 0 };
	memcpy(bad + 3, out + 3, DL_P25_SYS_RECORD);
	bad[3 + 8] = 0xFF; bad[3 + 9] = 0xFF; bad[3 + 10] = 0xFF;	// WACN above 20 bits
	assert(debuglink_cmd_p25_sys(bad, sizeof bad, out, sizeof out) == 1 && out[0] == DL_ST_BAD_ARG);
	assert(debuglink_cmd_p25_sys(get, sizeof get, out, 3u) == 1 && out[0] == DL_ST_BAD_ARG);

	return 0;
}
