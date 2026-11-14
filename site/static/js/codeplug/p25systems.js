// SPDX-License-Identifier: MIT
// Copyright (c) 2026 The RadioFoundry Authors
// The P25 trunking systems the radio keeps in its settings store (docs/spec/p25.md 8.1), as the web editor sees them.
// The wire form is DL_CMD_P25_SYS (docs/spec/debuglink.md 3.24): system records of 57 bytes, so the browser and the
// firmware agree without sharing C structures.

export const P25_SYSTEMS = 4;
export const P25_RECORD = 57;
export const P25_NAME = 17;

export const p25Hex = (v, digits) => Number(v || 0).toString(16).toUpperCase().padStart(digits, "0");
export const p25Mhz = (hz) => (hz ? (hz / 1e6).toFixed(5).replace(/0{1,2}$/, "") : "");

export function newP25System(name = "SYSTEM 1") {
	return {
		name: String(name).slice(0, P25_NAME - 1),
		ccHz: 438800000,
		uplinkHz: 433800000,
		wacn: 0xBEEF0,
		unitId: 2,
		sys: 0x123,
		nac: 0x123,
		tg: 1,
		rfss: 1,
		site: 1,
		bandAuto: true,
		valid: true,
		baseHz: 0,
		spacingHz: 0,
		offsetHz: 0,
	};
}

function put16(b, o, v) {
	b[o] = v & 0xFF;
	b[o + 1] = (v >> 8) & 0xFF;
}

function put32(b, o, v) {
	b[o] = v & 0xFF;
	b[o + 1] = (v >>> 8) & 0xFF;
	b[o + 2] = (v >>> 16) & 0xFF;
	b[o + 3] = (v >>> 24) & 0xFF;
}

function get16(b, o) {
	return b[o] | (b[o + 1] << 8);
}

function get32(b, o) {
	return (b[o] | (b[o + 1] << 8) | (b[o + 2] << 16) | (b[o + 3] << 24)) >>> 0;
}

function putSystem(b, o, s) {
	put32(b, o + 0, Number(s.ccHz) || 0);
	put32(b, o + 4, Number(s.uplinkHz) || 0);
	put32(b, o + 8, Number(s.wacn) || 0);
	put32(b, o + 12, Number(s.unitId) || 0);
	put32(b, o + 16, Number(s.baseHz) || 0);
	put32(b, o + 20, Number(s.spacingHz) || 0);
	put32(b, o + 24, Number(s.offsetHz) || 0);
	put16(b, o + 28, Number(s.sys) || 0);
	put16(b, o + 30, Number(s.nac) || 0);
	put16(b, o + 32, Number(s.tg) || 0);
	put16(b, o + 34, Number(s.rfss) || 0);
	put16(b, o + 36, Number(s.site) || 0);
	b[o + 38] = s.bandAuto ? 1 : 0;
	b[o + 39] = s.valid ? 1 : 0;
	const enc = new TextEncoder().encode(String(s.name || ""));
	b.fill(0, o + 40, o + 57);
	b.set(enc.subarray(0, P25_NAME - 1), o + 40);
}

function getSystem(b, o) {
	const name = new TextDecoder().decode(b.subarray(o + 40, o + 57));
	return {
		name: name.replace(/\0.*$/s, ""),
		ccHz: get32(b, o + 0),
		uplinkHz: get32(b, o + 4),
		wacn: get32(b, o + 8),
		unitId: get32(b, o + 12),
		baseHz: get32(b, o + 16),
		spacingHz: get32(b, o + 20),
		offsetHz: get32(b, o + 24) | 0,
		sys: get16(b, o + 28),
		nac: get16(b, o + 30),
		tg: get16(b, o + 32),
		rfss: get16(b, o + 34),
		site: get16(b, o + 36),
		bandAuto: b[o + 38] !== 0,
		valid: b[o + 39] !== 0,
	};
}

// A GET reply without the debug link status byte: count u8, active u8, the records
export function decodeP25Systems(bytes) {
	if (bytes.length < 2) throw new Error("P25 systems: short answer");
	const count = bytes[0];
	if (count < 1 || count > P25_SYSTEMS || bytes.length !== 2 + count * P25_RECORD) {
		throw new Error("P25 systems: malformed answer");
	}
	const systems = [];
	for (let i = 0; i < count; i++) systems.push(getSystem(bytes, 2 + i * P25_RECORD));
	const active = bytes[1] < count ? bytes[1] : 0;
	return { systems, active };
}

// The GET reply after the status byte: count, active, the records
export function encodeP25SystemsReply(systems, active = 0) {
	if (!systems.length || systems.length > P25_SYSTEMS) throw new Error("P25 systems: 1 to 4 are needed");
	const b = new Uint8Array(2 + systems.length * P25_RECORD);
	b[0] = systems.length;
	b[1] = active < systems.length ? active : 0;
	systems.forEach((s, i) => putSystem(b, 2 + i * P25_RECORD, s));
	return b;
}

// The SET payload: op, count, active, the records
export function encodeP25Systems(systems, active = 0) {
	const b = encodeP25SystemsReply(systems, active);
	const out = new Uint8Array(b.length + 1);
	out[0] = 1;
	out.set(b, 1);
	return out;
}

// Problems in the systems and the channels that name them; the page words the codes
export function validateP25Systems(m) {
	const out = [];
	const systems = m.systems || [];
	if (systems.length < 1) out.push({ level: "error", table: "systems", code: "p25_no_system" });
	if (systems.length > P25_SYSTEMS) out.push({ level: "error", table: "systems", code: "p25_too_many", params: { n: P25_SYSTEMS } });
	systems.forEach((s, i) => {
		const at = { table: "systems", item: s, index: i };
		if (!s.name) out.push({ ...at, level: "error", code: "no_name", field: "name" });
		if (s.name.length > P25_NAME - 1) out.push({ ...at, level: "error", code: "too_long", field: "name", params: { max: P25_NAME - 1 } });
		for (const f of ["ccHz", "uplinkHz"]) if (!(s[f] > 0 && s[f] < 1e9)) out.push({ ...at, level: "error", code: "bad_freq", field: f });
		for (const [f, max] of [["wacn", 0xFFFFF], ["nac", 0xFFF], ["sys", 0xFFF], ["tg", 0xFFFF], ["rfss", 0xFFFF], ["site", 0xFFFF]]) {
			if (!(Number.isInteger(s[f]) && s[f] >= 0 && s[f] <= max)) out.push({ ...at, level: "error", code: "range", field: f, params: { min: 0, max } });
		}
		if (!Number.isInteger(s.unitId) || s.unitId < 1 || s.unitId > 0xFFFFFFFF) out.push({ ...at, level: "error", code: "id_range", field: "unitId" });
		if (!s.bandAuto) {
			if (!(s.baseHz > 0 && s.baseHz < 1e9)) out.push({ ...at, level: "error", code: "bad_freq", field: "baseHz" });
			if (!(Number.isInteger(s.spacingHz) && s.spacingHz > 0)) out.push({ ...at, level: "error", code: "range", field: "spacingHz", params: { min: 1, max: 0xFFFFFFFF } });
		}
	});
	const need = (c) => c.p25 || (c.p25Conv && c.p25SystemSet);
	for (const c of m.channels || []) {
		if (need(c) && !(Number.isInteger(c.p25System) && c.p25System >= 0 && c.p25System < systems.length)) {
			out.push({ level: "error", table: "channels", item: c, field: "p25System", code: "p25_no_system_ref" });
		}
		if (c.p25Conv && !(c.p25Nac >= 0 && c.p25Nac <= 0xFFF)) out.push({ level: "error", table: "channels", item: c, field: "p25Nac", code: "range", params: { min: 0, max: 0xFFF } });
		if ((c.p25 || c.p25Conv) && !(c.p25Tg >= 0 && c.p25Tg <= 0xFFFF)) out.push({ level: "error", table: "channels", item: c, field: "p25Tg", code: "range", params: { min: 0, max: 0xFFFF } });
	}
	return out;
}
