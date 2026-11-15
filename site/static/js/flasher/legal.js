// SPDX-License-Identifier: MIT
// Copyright (c) 2026 The RadioFoundry Authors
// The legal disclaimer the page must accept before a task that writes to a radio, and the tasks it covers.
// It lives for the page's lifetime: Start Over keeps it, a reload asks again.

import { h, t, panel, button } from "./dom.js";

// The steps that write to the radio (backup and restart do not)
export const WRITE_TASKS = ["install", "update", "restore", "help"];

export const taskNeedsLegal = (task) => WRITE_TASKS.includes(task);

let accepted = false;

export const legalAccepted = () => accepted;

export const setLegalAccepted = (yes) => { accepted = !!yes; };

// The start page's panel: the terms, and the checkbox that lets a write task start. onChange redraws the page so the
// disabled task buttons follow the box.
export function legalPanel(onChange = () => {}, { onCancel = null } = {}) {
	const box = h("input", { type: "checkbox", checked: accepted, onchange: (e) => {
		setLegalAccepted(e.target.checked);
		onChange();
	} });
	return panel(t("flash.legal.title"),
		h("p", {}, t("flash.legal.intro")),
		h("div", { class: "legal-terms" },
			h("p", {}, t("flash.legal.amateur")),
			h("p", {}, t("flash.legal.permission")),
			h("p", {}, t("flash.legal.life")),
			h("p", {}, t("flash.legal.law"))),
		h("label", { class: "legal-accept" }, box, h("span", {}, t("flash.legal.accept"))),
		onCancel ? h("div", { class: "actions" }, button(t("flash.back"), onCancel, { quiet: true })) : null);
}
