// SPDX-License-Identifier: MIT
// Copyright (c) 2026 The RadioFoundry Authors
// Mockup-only interaction: screen switching, Home * FM/P25, and a held-PTT simulation. No radio I/O.
const screen = document.getElementById("screen");
const views = [...document.querySelectorAll("[data-screen]")];
const nav = [...document.querySelectorAll("[data-go]")];
const note = document.getElementById("screen-note");
const txCall = document.getElementById("tx-call");
const txMeta = document.getElementById("tx-meta");
const txTime = document.getElementById("tx-time");
const txPower = document.getElementById("tx-power");

const notes = {
	boot: "The board splash is the supplied 240×320 key art. It is shown for about one second; keys during it are ignored.",
	"home-fm": "Analog FM at rest. The frequency, tone, bandwidth and power are visible without entering a menu; * is the only mode switch.",
	"home-p25": "Conventional P25 at rest. Channel, NAC, talkgroup and unit are visible at a glance; PTT is the only talk action.",
	"home-trunk": "The same Home frame for a control-channel system: the CC, WACN, TG and unit ID replace the conventional fields. PTT requests a grant.",
	"rx-p25": "An incoming clear group call. RX is green, the caller and talkgroup are prominent, and the only action is PTT reply.",
	"tx-p25": "Held PTT. The timer and microphone meter answer “am I on air?”; release is a physical key action, not a touch target.",
	quick: "Quick settings are a short encoder list, not a grid of touch tiles. The selected row is the only edit target.",
	"p25-settings": "The P25 setup is a field list. Frequency, NAC, TG and unit fit a 240 px width at the small font without truncation.",
	tools: "Tools keeps the numbered-list convention. P25 voice and P25 system are first; the rest are the existing radio screens."
};

let current = "home-p25";
let lastScreen = "home-p25";
let pttTimer = null;
let pttStarted = 0;

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

function txFor(source) {
	if (source === "home-fm") {
		txCall.textContent = "446.0000";
		txMeta.innerHTML = "FM CARRIER <span>·</span> 12.5 kHz";
		txPower.textContent = "2.0 W · ANALOG";
	} else if (source === "home-trunk") {
		txCall.textContent = "TG 1";
		txMeta.innerHTML = "P25 TRUNKED <span>·</span> GRANTED";
		txPower.textContent = "2.0 W · RELEASE PTT";
	} else {
		txCall.textContent = "TG 1";
		txMeta.innerHTML = "P25 CLEAR <span>·</span> NAC 293";
		txPower.textContent = "2.0 W · RELEASE PTT";
	}
}

function tick() {
	const s = Math.max(0, Math.floor((Date.now() - pttStarted) / 1000));
	txTime.textContent = `00:${String(s % 60).padStart(2, "0")}`;
}

function pttDown(event) {
	if (event) {
		event.preventDefault();
	}
	if (pttStarted !== 0 || current === "tx-p25" || current === "boot") {
		return;
	}
	lastScreen = current === "rx-p25" ? "rx-p25" : current === "home-fm" ? "home-fm" :
		current === "home-trunk" ? "home-trunk" : "home-p25";
	txFor(lastScreen);
	pttStarted = Date.now();
	tick();
	pttTimer = setInterval(tick, 250);
	show("tx-p25");
}

function pttUp(event) {
	if (event) {
		event.preventDefault();
	}
	if (pttStarted === 0) {
		return;
	}
	clearInterval(pttTimer);
	pttTimer = null;
	pttStarted = 0;
	show(lastScreen);
}

function star() {
	if (current === "tx-p25" || current === "boot") {
		return;
	}
	show(current === "home-p25" ? "home-fm" : "home-p25");
}

for (const b of nav) {
	b.addEventListener("click", () => show(b.dataset.go));
}

document.querySelector('[data-action="star"]').addEventListener("click", star);
document.querySelector('[data-action="ptt"]').addEventListener("pointerdown", pttDown);
window.addEventListener("pointerup", pttUp);
window.addEventListener("pointercancel", pttUp);
window.addEventListener("keyup", event => {
	if (event.key === " " && pttStarted !== 0) {
		pttUp(event);
	}
});
window.addEventListener("keydown", event => {
	if (event.key === "*") {
		event.preventDefault();
		star();
	} else if (event.key === " " && pttStarted === 0) {
		event.preventDefault();
		pttDown(event);
	}
});

const initial = location.hash.slice(1);
show(views.some(v => v.dataset.screen === initial) ? initial : "home-p25");
window.addEventListener("hashchange", () => {
	const name = location.hash.slice(1);
	if (views.some(v => v.dataset.screen === name)) {
		show(name);
	}
});
