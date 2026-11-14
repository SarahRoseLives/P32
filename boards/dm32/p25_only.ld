/* SPDX-License-Identifier: MIT */
/* Copyright (c) 2026 The RadioFoundry Authors */
/* Baofeng DM-32UV, P25/FM image without DMR (CONFIG_DMR=n, config.mk selects this script; memory.ld is the DMR build's).
 *
 * Program flash is the same as memory.ld: the bootloader below the image, the application from 0x0300C000, and the
 * codec slot 0x030E8000..0x03100000, which this image never uses but leaves alone for the flashing tools.
 *
 * SRAM (288 KB at 0x10000): RadioFoundry uses the old application region and the unreachable part of the codec window,
 * 0x10000..0x56000. The top 8 KB, 0x56000..0x58000, is the runtime stub's load span (HRC_STUB_BASE / HRC_STUB_SIZE in
 * config.mk). The universal stub copies itself to 0x30000 when it starts, so it needs only the loaded copy here; the
 * firmware's own RAM is dead by then. This reclaims 75,264 B of the 83,456 B codec window (docs/spec/hardware-dm32.md
 * 17.1). The __codec_* symbols stay the stock codec's, since the tools read them from every DM-32UV linker script.
 *
 * The modem SRAM window the stock codec's tables use is still reserved; this image does not copy them. */

__codec_slot = 0x030E8000;
__codec_slot_end = 0x03100000;
/* The stock codec's windows (platform/hrc7000/codec_slot.h, docs/spec/codec-dm32.md). A DMR build runs the code at
 * __codec_run_lo; this build links its RAM part way into that window and leaves the last 8 KB for the runtime stub. */
__codec_run_lo = 0x00043A00;
__codec_run_hi = 0x00058000;
__codec_tab_lo = 0x18000E48;
__codec_tab_hi = 0x18003E28;

/* The application's RAM ends where the stub's load span starts (HRC_STUB_BASE in config.mk) */
__ram_end = 0x00056000;

MEMORY
{
	FLASH  (rx)  : ORIGIN = __app_start, LENGTH = __codec_slot - __app_start
	RAM    (rwx) : ORIGIN = 0x00010000, LENGTH = __ram_end - 0x00010000
}

INCLUDE common.ld
