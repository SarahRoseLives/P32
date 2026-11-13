// SPDX-License-Identifier: MIT
// Copyright (c) 2026 The RadioFoundry Authors
// Mockup interaction only: profiles, channel sheet, local-programming entry and a held-PTT simulation.
const screen = document.getElementById("screen");
const views = [...document.querySelectorAll("[data-screen]")];
const nav = [...document.querySelectorAll("[data-go]")];
const note = document.getElementById("screen-note");

const tx = {
	rail: document.getElementById("tx-rail-mode"),
	eyebrow: document.getElementById("tx-eyebrow"),
	label: document.getElementById("tx-label"),
	main: document.getElementById("tx-main"),
	sub: document.getElementById("tx-sub"),
	d1l: document.getElementById("tx-deck1-l"),
	d1: document.getElementById("tx-deck1"),
	d2l: document.getElementById("tx-deck2-l"),
	d2: document.getElementById("tx-deck2"),
	d3l: document.getElementById("tx-deck3-l"),
	d3: document.getElementById("tx-deck3")
};

const notes = {
	"home-conv": "A conventional channel: frequency and NAC are the operating facts; TG is carried in the call. The instrument does not look different from a trunked channel by accident — the blue edge and CNV tag say why.",
	"home-trunk": "A trunked channel: the talkgroup is the object, the system and control channel are the technical deck. Affiliation is a state, not a menu item.",
	"home-fm": "Analog FM is still a first-class channel. The same frame, a green edge and the tone/bandwidth deck keep FM from looking like a fallback.",
	rx: "Receive fills the same instrument with the caller. PTT is the only reply action; the technical deck stays in place instead of moving the operator to another screen.",
	tx: "Transmit is the same frame in amber. The timer answers “how long,” the meter answers “is my voice getting there,” and release is a physical key.",
	channels: "Channels are grouped by family but share one list. Conventional, trunked and analog channels are peers; selecting one replaces the instrument object.",
	program: "Local programming is a mode, not a hidden service menu. Categories stay on the left, the editable objects on the right; the encoder changes category, OK enters.",
	talkgroups: "Talkgroups belong to a trunked system, not to an individual channel. A channel stores the system and the selected talkgroup; a scan list can still hold several.",
	"edit-conv": "Conventional channel FPP: frequency, NAC, talkgroup, power, bandwidth and RX-only. The type strip makes the channel family explicit before any field is touched.",
	"edit-trunk": "Trunked channel FPP: the channel points at a system and a talkgroup. The system owns the CC, NAC, unit and band plan, so those fields do not repeat here.",
	"edit-system": "System FPP: one screen owns the control channel, WACN, SysID, RFSS/site, NAC, unit and band plan. A trunked channel only stores the system link and TG.",
	write: "FPP ends with an explicit write review. The operator sees exactly which conventional channel, trunked channel and system will change before committing."
};

let current = "home-conv";
let last = "home-conv";
let timer = null;
let started = 0;

function show(name) {
	const next = views.find(v => v.dataset.screen === name);
	if (!next) {
		return;
	}
	current = name;
	for (const v of views) {
		v.classList.toggle("is-active", v === next);
	}
	for (const b of nav) {
		b.classList.toggle("active", b.dataset.go === name);
	}
	note.textContent = notes[name] || "";
}

function txLabels(source) {
	if (source === "home-trunk") {
		tx.rail.textContent = "P25";
		tx.eyebrow.innerHTML = 'TRANSMITTING <span>/</span> TRUNKED';
		tx.label.innerHTML = 'GRANTED <span class="status-inline">TG 1</span>';
		tx.sub.innerHTML = 'SYSTEM <b>123</b> <span class="sep">·</span> UID <b>2</b>';
		tx.d1l.textContent = "CC";
		tx.d1.textContent = "438.8000";
		tx.d2l.textContent = "SYSTEM";
		tx.d2.textContent = "123";
		tx.d3l.textContent = "POWER";
		tx.d3.textContent = "2.0 W";
	} else if (source === "home-fm") {
		tx.rail.textContent = "FM";
		tx.eyebrow.innerHTML = 'TRANSMITTING <span>/</span> ANALOG';
		tx.label.innerHTML = 'ANALOG FM <span class="status-inline">12.5 kHz</span>';
		tx.sub.innerHTML = 'TONE <b>88.5</b> <span class="sep">·</span> POWER <b>2.0 W</b>';
		tx.d1l.textContent = "TONE";
		tx.d1.textContent = "88.5 Hz";
		tx.d2l.textContent = "BAND";
		tx.d2.textContent = "12.5 k";
		tx.d3l.textContent = "POWER";
		tx.d3.textContent = "2.0 W";
	} else {
		tx.rail.textContent = "P25";
		tx.eyebrow.innerHTML = 'TRANSMITTING <span>/</span> CONVENTIONAL';
		tx.label.innerHTML = 'CLEAR VOICE <span class="status-inline">NAC 293</span>';
		tx.sub.innerHTML = 'TALKGROUP <b>1</b> <span class="sep">·</span> UID <b>2</b>';
		tx.d1l.textContent = "MODE";
		tx.d1.textContent = "P25";
		tx.d2l.textContent = "TG";
		tx.d2.textContent = "1";
		tx.d3l.textContent = "POWER";
		tx.d3.textContent = "2.0 W";
	}
}

function tick() {
	const seconds = Math.max(0, Math.floor((Date.now() - started) / 1000));
	tx.main.innerHTML = `${String(Math.floor(seconds / 60)).padStart(2, "0")}:${String(seconds % 60).padStart(2, "0")}<span class="unit">HOLD PTT</span>`;
}

function pttDown(event) {
	if (event) {
		event.preventDefault();
	}
	if (started !== 0 || current === "tx") {
		return;
	}
	last = current === "rx" ? "rx" : current;
	txLabels(last);
	started = Date.now();
	tick();
	timer = setInterval(tick, 250);
	show("tx");
}

function pttUp(event) {
	if (event) {
		event.preventDefault();
	}
	if (started === 0) {
		return;
	}
	clearInterval(timer);
	timer = null;
	started = 0;
	show(last);
}

function cycleProfile() {
	if (started !== 0) {
		return;
	}
	if (current === "home-conv") {
		show("home-trunk");
	} else if (current === "home-trunk") {
		show("home-fm");
	} else {
		show("home-conv");
	}
}

for (const b of nav) {
	b.addEventListener("click", () => show(b.dataset.go));
}
document.querySelector('[data-action="profile"]').addEventListener("click", cycleProfile);
document.querySelector('[data-action="channels"]').addEventListener("click", () => show("channels"));
document.querySelector('[data-action="program"]').addEventListener("click", () => show("program"));
document.querySelector('[data-action="ptt"]').addEventListener("pointerdown", pttDown);
window.addEventListener("pointerup", pttUp);
window.addEventListener("pointercancel", pttUp);
window.addEventListener("keydown", event => {
	if (event.key === "*") {
		event.preventDefault();
		cycleProfile();
	} else if (event.key === " " && started === 0) {
		event.preventDefault();
		pttDown(event);
	}
});
window.addEventListener("keyup", event => {
	if (event.key === " " && started !== 0) {
		pttUp(event);
	}
});

const initial = location.hash.slice(1);
show(views.some(v => v.dataset.screen === initial) ? initial : "home-conv");
window.addEventListener("hashchange", () => {
	const name = location.hash.slice(1);
	if (views.some(v => v.dataset.screen === name)) {
		show(name);
	}
});
