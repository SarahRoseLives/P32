// SPDX-License-Identifier: ISC
// Copyright (c) 2026 The RadioFoundry Authors
// The TSBK field parser: the five control-channel blocks the portable SDR's p25_tx sends (defaults NAC 0x293,
// WACN 0xBEEF0, SYS 1, RFSS 1, SITE 1, TG 1, VCH 1, a 433.575 MHz identifier update).
#include <stdio.h>
#include <string.h>
#include "p25/p25_tsbk_parse.h"
#include "p25/p25_cc_tx.h"

static int failures;

#define CHECK(cond) do { if (!(cond)) { printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); failures++; } } while (0)

int main(void)
{
	p25_cc_t cc;

	// IdenUp 0x3D: the band frequency 433.575 MHz
	static const uint8_t iden[12] = { 0xBD, 0x00, 0x03, 0x20, 0x00, 0x64, 0x05, 0x2B, 0x2A, 0x78, 0xD4, 0xF2 };
	memset(&cc, 0, sizeof cc);
	CHECK(p25_tsbk_parse(iden, &cc));
	CHECK(cc.opcode == 0x3D);
	CHECK(cc.freq_hz == 433575000u);

	// Network Status 0x3B: WACN and system
	static const uint8_t net[12] = { 0xBB, 0x00, 0x00, 0xBE, 0xEF, 0x00, 0x01, 0x00, 0x00, 0x00, 0xB5, 0xA7 };
	memset(&cc, 0, sizeof cc);
	CHECK(p25_tsbk_parse(net, &cc));
	CHECK(cc.opcode == 0x3B);
	CHECK(cc.wacn == 0xBEEF0u);
	CHECK(cc.sys == 0x001);

	// RFSS Status 0x3A: system, RFSS and site
	static const uint8_t rfss[12] = { 0xBA, 0x00, 0x00, 0x00, 0x01, 0x01, 0x01, 0x00, 0x00, 0x00, 0xBB, 0xD4 };
	memset(&cc, 0, sizeof cc);
	CHECK(p25_tsbk_parse(rfss, &cc));
	CHECK(cc.opcode == 0x3A);
	CHECK(cc.sys == 0x001);
	CHECK(cc.rfss == 1);
	CHECK(cc.site == 1);

	// Group Voice Grant 0x00: channel, talkgroup and source (octets 2..3, 5..6 and 7..9)
	static const uint8_t grant[12] = { 0x80, 0x00, 0x10, 0x01, 0x00, 0x00, 0x01, 0x00, 0x00, 0x01, 0xA7, 0x79 };
	memset(&cc, 0, sizeof cc);
	CHECK(p25_tsbk_parse(grant, &cc));
	CHECK(cc.opcode == 0x00);
	CHECK(cc.tg == 1);
	CHECK(cc.vch == 1);
	CHECK(cc.source == 1);

	// Group Affiliation Response 0x28: the site's echo of our group and source
	uint8_t aff[12];
	p25_tsbk_group_affiliation(aff, 0x123, 1, 2);
	memset(&cc, 0, sizeof cc);
	CHECK(p25_tsbk_parse(aff, &cc));
	CHECK(cc.opcode == 0x28);
	CHECK(cc.affiliate_tg == 1);
	CHECK(cc.affiliate_source == 2);
	CHECK(cc.affiliation_seq == 1);

	// An unknown opcode is reported, not guessed
	static const uint8_t other[12] = { 0xFF, 0x00, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 };
	memset(&cc, 0, sizeof cc);
	CHECK(!p25_tsbk_parse(other, &cc));
	CHECK(cc.opcode == 0x7F);

	if (failures == 0) {
		printf("p25TsbkParseTest: all checks passed\n");
	}
	return failures == 0 ? 0 : 1;
}
