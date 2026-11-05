// SPDX-License-Identifier: MIT
// Copyright (c) 2026 The RadioFoundry Authors
// The P25 control-channel monitor's debug link section (GET_VIEW "p25"): the contents of the monitor screen, for the
// bench and the HIL test (docs/spec/p25.md 8). Its own section: the features section is already full.
#include "config.h"
#include "board.h"
#include "debuglink/debuglink.h"

void debuglink_view_p25(const radio_view_t *v, dl_text_t *t)
{
#if CONFIG_P25
	dl_text(t, "p25.on=%u\np25.valid=%u\np25.nac=%x\np25.duid=%x\np25.opcode=%x\n", v->p25.on, v->p25.valid,
		v->p25.nac, v->p25.duid, v->p25.opcode);
	dl_text(t, "p25.wacn=%x\np25.sys=%x\np25.rfss=%u\np25.site=%u\np25.tg=%u\np25.vch=%u\n", (unsigned)v->p25.wacn,
		(unsigned)v->p25.sys, v->p25.rfss, v->p25.site, v->p25.tg, v->p25.vch);
	dl_text(t, "p25.freq_hz=%u\np25.frames=%u\n", (unsigned)v->p25.freq_hz, (unsigned)v->p25.frames);
#else
	(void)v;
	dl_text(t, "p25.built=0\n");
#endif
}
