// SPDX-License-Identifier: MIT
// Copyright (c) 2026 The RadioFoundry Authors
// The P25 Codeplug Editor's records: FM, P25 conventional and P25 trunked channels, their zones, and the P25 systems
// they name. The systems are settings, not codeplug records, but the page edits them beside the channels.

import { parseFreq, mhz } from "../codeplug/fields.js";
import { LIMITS, POWER_NAMES } from "../codeplug/records.js";
import { p25Hex, p25Mhz } from "../codeplug/p25systems.js";
import { columns as dmrColumns, bulkColumns as dmrBulkColumns, formDialog, membersDialog } from "./views.js";
import { validate as validateBase } from "../codeplug/model.js";
import { validateP25Systems } from "../codeplug/p25systems.js";

const int = (lo, hi) => (t) => (r, k, v) => {
	const n = Number(String(v ?? "").trim());
	if (!Number.isInteger(n) || n < lo || n > hi) return t("editor.err.range", { min: lo, max: hi });
	r[k] = n;
};

const name = (max) => (t) => (r, k, v) => {
	v = String(v ?? "").trim();
	if (!v) return t("editor.err.no_name");
	if (v.length > max) return t("editor.err.too_long", { max });
	r[k] = v;
};

const freq = (t) => (r, k, v) => {
	const hz = parseFreq(v);
	if (hz === null) return t("editor.err.bad_freq");
	r[k] = hz;
};

const hex = (max, digits) => (t) => (r, k, v) => {
	const s = String(v ?? "").trim().replace(/^0x/i, "");
	if (!/^[0-9a-f]+$/i.test(s)) return t("editor.err.hex");
	const n = parseInt(s, 16);
	if (!Number.isInteger(n) || n < 0 || n > max) return t("editor.err.range", { min: 0, max });
	r[k] = n;
	return undefined;
};

const systemName = (m, idx) => m.systems?.[idx]?.name || "";

export const kindOf = (c) => (c.p25 ? "p25" : c.p25Conv ? "p25conv" : "fm");
export const kindText = (c, t) => t(`editor.kind.${kindOf(c)}`);
export const kindOptions = (t) => [["fm", t("editor.kind.fm")], ["p25", t("editor.kind.p25")], ["p25conv", t("editor.kind.p25conv")]];
export function setKind(r, v) {
	r.p25 = v === "p25";
	r.p25Conv = v === "p25conv";
	if (!r.p25Conv) r.p25SystemSet = false;
	if (r.p25 && !r.p25Tg) r.p25Tg = 1;
	if (r.p25Conv && !r.p25Nac) r.p25Nac = 0x123;
	if (r.p25Conv && !r.p25Tg) r.p25Tg = 1;
}

// The same channels, zones and systems tables; DMR-only tables are empty on this build
export function columns(table, m, t) {
	if (table !== "channels" && table !== "zones" && table !== "systems") return dmrColumns(table, m, t);
	const col = (key, label, type, parse, extra = {}) => ({
		key, label: t(label), type, get: (r) => r[key], set: parse ? (r, v) => parse(r, key, v) : undefined,
		readOnly: !parse, ...extra,
	});
	if (table === "systems") {
		return [
			{ key: "index", label: t("editor.col.number"), type: "number", readOnly: true, width: "3.5rem", get: (r) => m.systems.indexOf(r) + 1 },
			col("name", "editor.col.name", "text", name(16)(t)),
			col("ccHz", "editor.col.control", "text", freq(t), { format: p25Mhz, numeric: true }),
			col("uplinkHz", "editor.col.uplink", "text", freq(t), { format: p25Mhz, numeric: true }),
			col("nac", "editor.col.nac", "text", hex(0xFFF, 3)(t), { format: (v) => p25Hex(v, 3) }),
			col("wacn", "editor.col.wacn", "text", hex(0xFFFFF, 5)(t), { format: (v) => p25Hex(v, 5) }),
			col("sys", "editor.col.sys", "text", hex(0xFFF, 3)(t), { format: (v) => p25Hex(v, 3) }),
			col("tg", "editor.col.tg", "text", int(0, 0xFFFF)(t)),
			col("unitId", "editor.col.unit", "text", int(1, 0xFFFFFFFF)(t)),
			{ key: "band", label: t("editor.col.band_plan"), type: "text", readOnly: true, get: (r) => t(r.bandAuto ? "editor.band.auto" : "editor.band.manual") },
		];
	}
	if (table === "zones") {
		return [
			col("name", "editor.col.name", "text", name(LIMITS.name)(t)),
			{ key: "count", label: t("editor.col.channels"), type: "number", readOnly: true, get: (r) => r.channels.length, width: "7rem" },
			{ key: "list", label: t("editor.col.members"), type: "text", readOnly: true, get: (r) => r.channels.map((c) => (typeof c === "number" ? t("editor.missing", { n: c }) : c.name)).join(", ") },
		];
	}
	const isP25 = (r) => !!(r.p25 || r.p25Conv);
	return [
		{ key: "number", label: "#", type: "number", get: (r) => r.number, readOnly: true, width: "3.5rem", format: (v) => v ?? "–" },
		col("name", "editor.col.name", "text", name(LIMITS.name)(t)),
		{ key: "kind", label: t("editor.col.kind"), type: "select", get: kindOf, options: () => kindOptions(t), format: (v) => t(`editor.kind.${v}`),
			set: setKind },
		{ key: "p25System", label: t("editor.col.system"), type: "select", get: (r) => r.p25System, readOnly: (r) => !(r.p25 || (r.p25Conv && r.p25SystemSet)),
			options: () => [[0, t("editor.none")], ...(m.systems || []).map((s, i) => [i, s.name || `${t("editor.tab.systems")} ${i + 1}`])],
			format: (v, r) => (r.p25 || (r.p25Conv && r.p25SystemSet)) ? (systemName(m, v) || String(v)) : r.p25Conv ? "CNV" : "",
			set: (r, v) => (r.p25System = Number(v)) },
		{ key: "p25Tg", label: t("editor.col.tg"), type: "text", get: (r) => r.p25Tg, readOnly: (r) => !isP25(r), set: (r, v) => int(0, 0xFFFF)(t)(r, "p25Tg", v), format: (v, r) => (isP25(r) ? String(v ?? "") : "") },
		{ key: "p25Nac", label: t("editor.col.nac"), type: "text", get: (r) => r.p25Nac, readOnly: (r) => !r.p25Conv || r.p25SystemSet,
			set: (r, v) => hex(0xFFF, 3)(t)(r, "p25Nac", v), format: (v, r) => (r.p25Conv && !r.p25SystemSet ? p25Hex(v, 3) : "") },
		col("rxHz", "editor.col.rx", "text", (r, k, v) => {
			const was = r.rxHz;
			const err = freq(t)(r, k, v);
			if (!err && r.txHz === was) r.txHz = r.rxHz;
			return err;
		}, { format: mhz, numeric: true }),
		col("txHz", "editor.col.tx", "text", freq(t), { format: mhz, numeric: true }),
		col("power", "editor.col.power", "select", (r, k, v) => (r[k] = Number(v)), { options: () => powerOptions(t) }),
		col("rxOnly", "editor.col.rx_only", "bool", (r, k, v) => (r[k] = !!v)),
	];
}

export function bulkColumns(table, m, t) {
	if (table === "channels") return columns(table, m, t).filter((c) => c.set && !["name", "number"].includes(c.key));
	if (table === "systems") return columns(table, m, t).filter((c) => c.set && !["name", "index"].includes(c.key));
	return dmrBulkColumns(table, m, t);
}

function powerOptions(t) {
	const opts = [[0, t("editor.power.default")]];
	for (let n = 1; n <= 10; n++) opts.push([n, POWER_NAMES[n] ? t(`editor.power.${POWER_NAMES[n].toLowerCase()}`) : t("editor.power.level", { n: n - 1 })]);
	return opts;
}

function systemOptions(m, t) {
	return (m.systems || []).map((s, i) => [i, s.name || `${t("editor.tab.systems")} ${i + 1}`]);
}

export function channelForm(m, t) {
	const sel = (key, label, options, extra = {}) => ({ key, label: t(label), type: "select", options, ...extra });
	const chk = (key, label) => ({ key, label: t(label), type: "bool" });
	const txt = (key, label, parse, format) => ({ key, label: t(label), type: "text", parse, format });
	const fm = (f) => ({ ...f, show: (r) => kindOf(r) === "fm" });
	const p25 = (f) => ({ ...f, show: (r) => kindOf(r) !== "fm" });
	const trunk = (f) => ({ ...f, show: (r) => kindOf(r) === "p25" });
	const conv = (f) => ({ ...f, show: (r) => kindOf(r) === "p25conv" });
	const profile = (f) => ({ ...f, show: (r) => kindOf(r) === "p25conv" && r.p25SystemSet });
	const own = (f) => ({ ...f, show: (r) => kindOf(r) === "p25conv" && !r.p25SystemSet });
	return [
		{ group: t("editor.form.basic") },
		txt("name", "editor.col.name", name(LIMITS.name)(t)),
		sel("kind", "editor.col.kind", () => kindOptions(t), { get: kindOf,
			set: setKind }),
		sel("power", "editor.col.power", () => powerOptions(t), { set: (r, v) => (r.power = Number(v)) }),
		{ type: "pair", key: "freqs", fields: [txt("rxHz", "editor.col.rx", freq(t), mhz), { ...txt("txHz", "editor.col.tx", freq(t), mhz), follows: "rxHz" }], buttons: [
			["→", t("editor.freq.rx_to_tx"), ([rx]) => [rx, rx]],
			["⇄", t("editor.freq.swap"), ([rx, tx]) => [tx, rx]],
			["←", t("editor.freq.tx_to_rx"), ([, tx]) => [tx, tx]],
		] },
		txt("tot", "editor.col.tot", (r, k, v) => {
			const s = String(v ?? "").trim();
			if (!s || s === "0" || s.toLowerCase() === t("editor.tot.default").toLowerCase()) return void (r[k] = 0);
			const n = Number(s);
			if (!Number.isInteger(n) || n < 1 || n > 3825) return t("editor.err.tot");
			r[k] = Math.ceil(n / 15) * 15;
		}, (v) => (v ? String(v) : t("editor.tot.default"))),
		chk("rxOnly", "editor.col.rx_only"),
		fm({ group: t("editor.form.analog") }),
		fm(sel("bw", "editor.col.bw", () => [[12.5, "12.5 kHz"], [25, "25 kHz"]], { set: (r, v) => (r.bw = Number(v)) })),
		fm(txt("rxTone", "editor.col.rx_tone", (r, k, v) => { const s = String(v ?? "").trim(); r[k] = s === "None" ? "" : s; }, (v) => v || t("editor.none"))),
		fm(txt("txTone", "editor.col.tx_tone", (r, k, v) => { const s = String(v ?? "").trim(); r[k] = s === "None" ? "" : s; }, (v) => v || t("editor.none"))),
		p25({ group: t("editor.form.p25") }),
		conv(chk("p25SystemSet", "editor.p25.use_system")),
		p25(sel("p25System", "editor.col.system", () => systemOptions(m, t), { set: (r, v) => (r.p25System = Number(v)),
			show: (r) => r.p25 || (r.p25Conv && r.p25SystemSet) })),
		profile({ type: "note", key: "p25_profile_note", text: () => t("editor.p25.profile_note") }),
		p25(txt("p25Tg", "editor.col.tg", int(0, 0xFFFF)(t), (v) => String(v ?? ""))),
		own(txt("p25Nac", "editor.col.nac", hex(0xFFF, 3)(t), (v) => p25Hex(v, 3))),
		{ group: t("editor.form.scan") },
		chk("zoneSkip", "editor.col.zone_skip"),
		chk("allSkip", "editor.col.all_skip"),
	];
}

export function systemForm(t) {
	const sel = (key, label, options, extra = {}) => ({ key, label: t(label), type: "select", options, ...extra });
	const chk = (key, label) => ({ key, label: t(label), type: "bool" });
	const txt = (key, label, parse, format) => ({ key, label: t(label), type: "text", parse, format });
	const manual = (f) => ({ ...f, show: (r) => !r.bandAuto });
	return [
		{ group: t("editor.form.system") },
		txt("name", "editor.col.name", name(16)(t)),
		txt("ccHz", "editor.col.control", freq(t), p25Mhz),
		txt("uplinkHz", "editor.col.uplink", freq(t), p25Mhz),
		txt("nac", "editor.col.nac", hex(0xFFF, 3)(t), (v) => p25Hex(v, 3)),
		txt("wacn", "editor.col.wacn", hex(0xFFFFF, 5)(t), (v) => p25Hex(v, 5)),
		txt("sys", "editor.col.sys", hex(0xFFF, 3)(t), (v) => p25Hex(v, 3)),
		txt("rfss", "editor.col.rfss", int(0, 0xFFFF)(t), (v) => String(v ?? "")),
		txt("site", "editor.col.site", int(0, 0xFFFF)(t), (v) => String(v ?? "")),
		txt("tg", "editor.col.tg", int(0, 0xFFFF)(t), (v) => String(v ?? "")),
		txt("unitId", "editor.col.unit", int(1, 0xFFFFFFFF)(t), (v) => String(v ?? "")),
		{ group: t("editor.form.band_plan") },
		chk("bandAuto", "editor.band.auto"),
		manual(txt("baseHz", "editor.col.base", freq(t), p25Mhz)),
		manual(txt("spacingHz", "editor.col.spacing", int(1, 0xFFFFFFFF)(t), (v) => String(v ?? ""))),
		manual(txt("offsetHz", "editor.col.offset", (r, k, v) => {
			const n = Number(String(v ?? "").trim());
			if (!Number.isInteger(n) || Math.abs(n) > 2147483647) return t("editor.err.range", { min: -2147483647, max: 2147483647 });
			r[k] = n;
		}, (v) => String(v ?? ""))),
	];
}

// P25 channels and their systems: the shared checks for names, frequencies and zones, then the P25 fields
export function validateP25(m, opts = {}) {
	const out = validateBase(m, opts).filter((p) => p.table !== "general" && p.code !== "listen_dmr_air" && p.code !== "no_dmr_id");
	return [...out, ...validateP25Systems(m)];
}

export { formDialog, membersDialog, refLabel } from "./views.js";
