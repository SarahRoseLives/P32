# SPDX-License-Identifier: MIT
# Copyright (c) 2026 The RadioFoundry Authors
# DM-32UV feature defaults (config/features.mk lists them all). 880 KB of code space before the codec slot, so RAM is
# the limit. The P25/FM image has no codec to run, so it links to 0x56000 instead of stopping at the codec window:
# 286,720 B, with the top 8 KB left for the runtime stub (boards/dm32/p25_only.ld, docs/spec/hardware-dm32.md 17.1).
# The RTOS heap is 116 KB (freertos_board.h); make size shows the static headroom left for other buffers. Every feature
# the radio has hardware and glue for is on, except those the product leaves out below.
# The host link runs on the programming cable's UART0; USB, PPP and TCP_CONNS follow the platform
# (platform/hrc7000/config.mk)
# P32Firmware is P25/FM only: DMR and the services that follow it are left out. The HR_C7000 modem glue and PCM audio
# stay on because the P25 receiver and transmitter use them (CONFIG_HRC_MODEM).
CONFIG_DMR ?= n
# Without DMR there is no stock codec to run, so the image can use the codec's RAM window. The runtime stub still
# needs a home above RadioFoundry: 8 KB at the top, enough for the universal monitor's 7,784-byte loaded copy. A DMR
# build keeps boards/dm32/memory.ld and the platform's 0x44000/80 KB stub span (platform/hrc7000/platform.h).
ifneq ($(CONFIG_DMR),y)
LDSCRIPT := boards/dm32/p25_only.ld
MCU_DEFS += -DHRC_STUB_BASE=0x00056000u -DHRC_STUB_SIZE=0x00002000u
endif
CONFIG_HRC_MODEM ?= y
CONFIG_P25 ?= y
CONFIG_P25_VOICE ?= y
CONFIG_P25_TX ?= y
CONFIG_P25_STAR ?= y
# APRS and GPS are not part of the P25/FM product. Their RAM is needed by the P25 voice codec and transmitter; the
# code exists, but P32 leaves it out. BOARD_NOT_WRITTEN keeps the board registry's hardware-switch check satisfied.
CONFIG_APRS ?= n
CONFIG_GPS ?= n
BOARD_NOT_WRITTEN += APRS_RX	# not in the P25/FM image; the RAM goes to P25 voice
BOARD_NOT_WRITTEN += GPS	# not in the P25/FM image; the P25 product has no location features
BOARD_NOT_WRITTEN += GPS_SKY	# not in the P25/FM image; follows GPS
# The large-panel instrument Home is the P32 face of the radio.
CONFIG_UI_INSTRUMENT ?= y
# The 240x320 panel at full resolution in 16 colors (display_dm32.c, board.mk). The 16x32 font is the large class's
# channel name; text is anti-aliased, and lists and labels are one size up (9x18)
CONFIG_FONT_XL ?= y
CONFIG_FONT_AA ?= y
CONFIG_FONT_BIG ?= y
# The bezel hides the panel's edges, in panel pixels: text 6 px from the left or right edge was still clipped on the
# radio, so 10 px left and right (the 8 px measured on the right leaves no more 8 px columns), 5 px top and bottom
CONFIG_DISPLAY_INSET_LEFT ?= 10
CONFIG_DISPLAY_INSET_RIGHT ?= 10
CONFIG_DISPLAY_INSET_TOP ?= 5
CONFIG_DISPLAY_INSET_BOTTOM ?= 5
# VOX reads the microphone level through hal_power_t.mic_level, which this board doesn't have yet
CONFIG_VOX ?= n
# The platform's three TCP connections; a fourth costs about 1.4 KB of main RAM. ci's maximal configuration has
# every switch but USB and USB_NETWORK (platform/hrc7000/config.mk), and NAND_STORAGE (below), and also keeps about
# 0.5 KB free
BOARD_UNSUPPORTED += NAND_STORAGE	# the data flash is SPI NOR, GD25Q128 class (docs/spec/hardware-dm32.md 10)
# What the hardware can do but this radio's driver or glue doesn't yet (docs/build-config.md, "Unsupported, not
# written, unconfirmed"): off by default, and off in ci's maximal configuration too
BOARD_NOT_WRITTEN += VOX	# no hal_power_t.mic_level hook yet
BOARD_NOT_WRITTEN += AUDIO_PROBE	# no board.audio_tap yet
