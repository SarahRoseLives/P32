// SPDX-License-Identifier: MIT
// Copyright (c) 2026 The RadioFoundry Authors
// The P25 trunking systems the editor reads and writes (static/js/codeplug/p25systems.js): the wire records, the
// channel references that name them, and the problems the editor reports before a write.

import { test } from "node:test";
import assert from "node:assert/strict";

import { newP25System, decodeP25Systems, encodeP25Systems, encodeP25SystemsReply, validateP25Systems } from "../static/js/codeplug/p25systems.js";
import { empty, newChannel } from "../static/js/codeplug/model.js";
import { FakeCpsRadio } from "../static/js/codeplug/fakecps.js";
import { CpsSerial } from "../static/js/codeplug/transport.js";

test("P25 systems round-trip through the 57-byte debug link records", () => {
	const a = newP25System("PARROT");
	a.ccHz = 438800000;
	a.uplinkHz = 433800000;
	a.nac = 0x293;
	a.tg = 10;
	a.unitId = 3144613;
	const b = newP25System("SITE 2");
	b.ccHz = 439000000;
	b.wacn = 0x12345;
	b.sys = 0x456;
	b.nac = 0xABC;
	b.bandAuto = false;
	b.baseHz = 851000000;
	b.spacingHz = 12500;
	b.offsetHz = -5000000;
	const bytes = encodeP25Systems([a, b], 1);
	assert.equal(bytes.length, 3 + 2 * 57);
	assert.deepEqual(decodeP25Systems(encodeP25SystemsReply([a, b], 1)), { systems: [a, b], active: 1 });
	assert.equal(decodeP25Systems(encodeP25SystemsReply([a, b], 1)).systems[1].offsetHz, -5000000);
});

test("a channel's P25 system is checked against the systems the codeplug has", () => {
	const m = empty();
	m.systems = [newP25System("ONE")];
	m.channels.push(newChannel({ name: "TRK", p25: true, p25System: 0, p25Tg: 10 }));
	m.channels.push(newChannel({ name: "CNV", p25Conv: true, p25Nac: 0x293, p25Tg: 1 }));
	assert.equal(validateP25Systems(m).length, 0);
	m.channels[0].p25System = 2;
	m.channels[1].p25Nac = 0x1000;
	const codes = validateP25Systems(m).map((p) => p.code);
	assert.deepEqual(codes, ["p25_no_system_ref", "range"]);
	m.systems = [];
	assert.ok(validateP25Systems(m).some((p) => p.code === "p25_no_system"));
});

test("the transport reads and writes the systems a simulated radio keeps", async () => {
	const radio = new FakeCpsRadio({ board: "DM-32UV", base: 0x700000 });
	const tr = new CpsSerial(radio.stream());
	const got = await tr.readP25Systems();
	assert.equal(got.systems.length, 1);
	assert.equal(got.systems[0].name, "SITE 1");
	const next = newP25System("PARROT SYS");
	next.nac = 0x293;
	next.tg = 10;
	await tr.writeP25Systems([next], 0);
	assert.equal(radio.systemWrites, 1);
	assert.equal(radio.systems[0].name, "PARROT SYS");
	assert.equal(radio.systems[0].nac, 0x293);
	assert.equal(radio.systems[0].tg, 10);
});
