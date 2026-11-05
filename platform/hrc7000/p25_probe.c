// SPDX-License-Identifier: MIT
// Copyright (c) 2026 The RadioFoundry Authors
// HR_C7000 P25 physical-layer receive bench probe (docs/spec/p25.md 5.1, modem-hrc7000.md 9.6). The chip's PHY receive
// test mode is documented for DMR bit-error tests: WORK_MODE 0x02 (continuous), RF_MODE 0xA0000060 (bit 31 and 29 set,
// two-point transmit, IF receive), LAYER2_CONTROL 0x40, LAYER2_TXRX_CTRL 0x41, then the deframed information in the RX
// RAM at +0x30 after the chip finds a sync. The P25 frame sync is 48 bits, the same width as DMR's, so the probe puts
// it in the receive-sync registers (and three byte-order variants in the others) and lets the user see whether the
// chip's deframer accepts it. The block the chip leaves is what we need to know before writing a P25 receiver.
#include "config.h"
#if CONFIG_P25_PROBE

#include <string.h>
#include "platform.h"
#include "board.h"
#include "p25_probe.h"
#include "dmr_glue.h"
#include "debuglink/dlog.h"
#include "p25/p25_bch.h"

#define C7_PHY_RECEIVE_STATE	0x1C0u
#define RX_INFO_OFF		0x30u	// RECV_INFO in the RX RAM (datasheet table 47)
#define RX_INFO_BYTES		36u	// 0x30..0x53: the CACH and the 264 deframed bits
// PHY_RECV_ABNORMAL / PHY_RDY / PHY_RX: the datasheet numbers them 8, 9 and 10, the project's DMR notes 11, 12 and
// 16. Enable all of them; the list tells which fired
#define PHY_BITS		((1u << 8) | (1u << 9) | (1u << 10) | (1u << 11) | (1u << 12) | (1u << 16))

static hrc7000_t *modem;
static bool on;
static struct {
	uint32_t rf_mode, work_mode, l2_control, txrx, sys_mask;
	uint32_t sync[8];
	bool dmr;
} saved;
static uint32_t type_info, phy_state, last_list, blocks;
// Consecutive PHY_RX blocks are contiguous 264-bit chunks of the demodulated stream; the P25 frame sync and NID
// straddle block boundaries, so keep several (docs/spec/p25.md). 7 blocks = 231 bytes fits the read's uint8_t length
#define P25_BLOCK_BYTES		33u	// the 264 deframed bits
#define P25_STREAM_BLOCKS	7u
#define P25_STREAM_BYTES	(P25_BLOCK_BYTES * P25_STREAM_BLOCKS)
#define P25_NID_REL		88u	// first-block NID start, 88 bits after the 48-bit sync (bench 2026-11-04)
static uint8_t stream[P25_STREAM_BYTES];
static uint16_t stream_len;
static bool cont_mode;

// Only PHY_RX (project bit 11) writes a block. PHY_RDY (bit 12) is the slot clock and PHY_RECV_ABNORMAL (bit 16)
// fires on its own; taking their stale RX RAM was the "block that never changes" (bench 2026-11-04)
#define PHY_RX_BIT	(1u << 11)

static void probe_isr(uint32_t list)
{
	if (!on || (list & PHY_RX_BIT) == 0) {
		return;
	}
	type_info = hrc7000_rd(modem, C7_RX_TYPE_INFO);
	phy_state = hrc7000_rd(modem, C7_PHY_RECEIVE_STATE);
	if (stream_len + P25_BLOCK_BYTES <= sizeof stream) {
		// Continuous mode: CACH at 0x30..0x32, the 264 bits at 0x33. Timeslot mode: the 264 bits at 0x30
		uint16_t off = (uint16_t)(C7_RX_RAM + (cont_mode ? 0x33u : RX_INFO_OFF));
		for (uint8_t i = 0; i < P25_BLOCK_BYTES; i++) {
			stream[stream_len + i] = modem->buf[off + i];
		}
		stream_len = (uint16_t)(stream_len + P25_BLOCK_BYTES);
	}
	last_list = list;
	blocks++;
	// The PHY receive test mode can raise its interrupts continuously when no sync is found. Stop after a few blocks
	// so the storm can never starve the host link; `stop` and `start` capture another burst
	if (blocks >= 32) {
		hrc7000_wr(modem, C7_SYS_INTERP_MASK, saved.sys_mask & ~PHY_BITS);
	}
}

void hrc_p25_probe_attach(hrc7000_t *m)
{
	modem = m;
}

static bool start_cfg(uint8_t variant)
{
	if (modem == NULL || on || hrc_dmr_tx_active()) {
		return false;
	}
	bool continuous = (variant & 1u) != 0;
	bool dual_phy = (variant & 2u) != 0;
	// The board routes the RF chip's audio to the modem only in DMR mode (the FM speaker path takes it off the ADC);
	// the caller tunes in DMR mode. Stop the DMR engine so its slot interrupts don't fight the PHY receiver
	saved.dmr = hrc_dmr_active();
	if (saved.dmr) {
		hrc_dmr_leave();
	}
	uint32_t irq = hrc_modem_irqs_mask();
	saved.rf_mode = hrc7000_rd(modem, C7_RF_MODE);
	saved.work_mode = hrc7000_rd(modem, C7_WORK_MODE);
	saved.l2_control = hrc7000_rd(modem, C7_LAYER2_CONTROL);
	saved.txrx = hrc7000_rd(modem, C7_LAYER2_TXRX_CTRL);
	saved.sys_mask = hrc7000_rd(modem, C7_SYS_INTERP_MASK);
	saved.sync[0] = hrc7000_rd(modem, C7_RECV_MS_SYNC_H);
	saved.sync[1] = hrc7000_rd(modem, C7_RECV_MS_SYNC_L);
	saved.sync[2] = hrc7000_rd(modem, C7_RECV_BS_SYNC_H);
	saved.sync[3] = hrc7000_rd(modem, C7_RECV_BS_SYNC_L);
	saved.sync[4] = hrc7000_rd(modem, C7_RECV_TDMA1_SYNC_H);
	saved.sync[5] = hrc7000_rd(modem, C7_RECV_TDMA1_SYNC_L);
	saved.sync[6] = hrc7000_rd(modem, C7_RECV_TDMA2_SYNC_H);
	saved.sync[7] = hrc7000_rd(modem, C7_RECV_TDMA2_SYNC_L);

	blocks = 0;
	stream_len = 0;
	cont_mode = continuous;
	type_info = phy_state = last_list = 0;
	on = false;						// no capture until the set-up is complete

	hrc7000_wr(modem, C7_WORK_MODE, C7_WORK_MODE_FM);	// reset the state machine, then PHY receive
	// Bits 31 and 29 are what the datasheet requires for PHY receive. The datasheet's 0xA0000060 assumes a digital IQ
	// feed; this board feeds the modem the RF chip's demodulated AF, so keep the working mode's AF/gain/IQ-invert bits.
	// Keep bit 11 (use_dual_phy): the DCDM direct-mode path is the one that put the P25 frame sync into the info bytes
	uint32_t rf = saved.rf_mode | 0xA0000000u;

	if (!dual_phy) {
		rf &= ~0x800u;		// only the MS/BS sync path, which strips the sync
	}
	hrc7000_wr(modem, C7_RF_MODE, rf);
	// The P25 frame sync in the MS/BS slots and, complemented, in the TDMA1/2 slots: the TDMA path is the raw window
	hrc7000_wr(modem, C7_RECV_MS_SYNC_H, 0x5575F5u | 0x80000000u);
	hrc7000_wr(modem, C7_RECV_MS_SYNC_L, 0xFF77FFu);
	hrc7000_wr(modem, C7_RECV_BS_SYNC_H, 0xFF77FFu | 0x80000000u);
	hrc7000_wr(modem, C7_RECV_BS_SYNC_L, 0x5575F5u);
	hrc7000_wr(modem, C7_RECV_TDMA1_SYNC_H, 0xAA8A0Au | 0x80000000u);
	hrc7000_wr(modem, C7_RECV_TDMA1_SYNC_L, 0x008800u);
	hrc7000_wr(modem, C7_RECV_TDMA2_SYNC_H, 0x008800u | 0x80000000u);
	hrc7000_wr(modem, C7_RECV_TDMA2_SYNC_L, 0xAA8A0Au);
	hrc7000_wr(modem, C7_WORK_MODE, continuous ? 0x02u : 0x22u);	// physical layer, continuous / timeslot
	hrc7000_wr(modem, C7_LAYER2_CONTROL, 0x40u);		// passive receive
	hrc7000_wr(modem, C7_LAYER2_TXRX_CTRL, 0x41u);		// passive receive enable
	hrc7000_wr(modem, C7_SYS_INTERP_MASK, saved.sys_mask | PHY_BITS);
	hrc_modem_irqs_restore(irq);
	// After the restore: it puts back the PIC mask from before the set-up, which had the modem source off. Enabling
	// the hook earlier was undone here, so no interrupt ever reached the probe (bench 2026-11-04)
	on = true;
	hrc_dmr_sys_hook(probe_isr);
	dlog("p25p wm %08x rf %08x l2 %08x tx %08x", (unsigned)hrc7000_rd(modem, C7_WORK_MODE),
	     (unsigned)hrc7000_rd(modem, C7_RF_MODE), (unsigned)hrc7000_rd(modem, C7_LAYER2_CONTROL),
	     (unsigned)hrc7000_rd(modem, C7_LAYER2_TXRX_CTRL));
	dlog("p25p ms %08x %08x bs %08x %08x td %08x %08x %08x %08x",
	     (unsigned)hrc7000_rd(modem, C7_RECV_MS_SYNC_H), (unsigned)hrc7000_rd(modem, C7_RECV_MS_SYNC_L),
	     (unsigned)hrc7000_rd(modem, C7_RECV_BS_SYNC_H), (unsigned)hrc7000_rd(modem, C7_RECV_BS_SYNC_L),
	     (unsigned)hrc7000_rd(modem, C7_RECV_TDMA1_SYNC_H), (unsigned)hrc7000_rd(modem, C7_RECV_TDMA1_SYNC_L),
	     (unsigned)hrc7000_rd(modem, C7_RECV_TDMA2_SYNC_H), (unsigned)hrc7000_rd(modem, C7_RECV_TDMA2_SYNC_L));
	return true;
}

static bool start(void)
{
	return start_cfg(3u);	// continuous, dual-PHY receive
}

static void stop(void)
{
	if (modem == NULL || !on) {
		return;
	}
	on = false;
	hrc_dmr_sys_hook(NULL);
	uint32_t irq = hrc_modem_irqs_mask();
	hrc7000_wr(modem, C7_SYS_INTERP_MASK, saved.sys_mask);
	hrc7000_wr(modem, C7_LAYER2_TXRX_CTRL, saved.txrx);
	hrc7000_wr(modem, C7_LAYER2_CONTROL, saved.l2_control);
	hrc7000_wr(modem, C7_RECV_MS_SYNC_H, saved.sync[0]);
	hrc7000_wr(modem, C7_RECV_MS_SYNC_L, saved.sync[1]);
	hrc7000_wr(modem, C7_RECV_BS_SYNC_H, saved.sync[2]);
	hrc7000_wr(modem, C7_RECV_BS_SYNC_L, saved.sync[3]);
	hrc7000_wr(modem, C7_RECV_TDMA1_SYNC_H, saved.sync[4]);
	hrc7000_wr(modem, C7_RECV_TDMA1_SYNC_L, saved.sync[5]);
	hrc7000_wr(modem, C7_RECV_TDMA2_SYNC_H, saved.sync[6]);
	hrc7000_wr(modem, C7_RECV_TDMA2_SYNC_L, saved.sync[7]);
	hrc7000_wr(modem, C7_RF_MODE, saved.rf_mode);
	hrc7000_wr(modem, C7_WORK_MODE, saved.work_mode);
	hrc_modem_irqs_restore(irq);
	if (saved.dmr) {
		hrc_dmr_enter();	// the radio core still thinks it is in DMR: give it its receiver back
	}
}

static uint8_t stream_bit(uint16_t bit)
{
	return (uint8_t)((stream[bit >> 3] >> (7u - (bit & 7u))) & 1u);
}

// The continuous-mode stream is a sliding window of the demodulated P25 symbols. Find the frame sync, then the NID:
// after the sync the chip inserts a DMR-shaped gap, so the NID starts at a variable offset (88 bits on the bench,
// p25.md 5.2); try a range and keep the codeword with the fewest corrected bits. Bench 2026-11-04: every capture
// decoded NAC 0x293 DUID 0x7 with 3-4 corrected bits.
static bool p25_nid_scan(uint16_t *nac, uint8_t *duid, uint8_t *errs)
{
	static const uint8_t fsw[24] = {
		1, 1, 1, 1, 1, 3, 1, 1, 3, 3, 1, 1, 3, 3, 3, 3, 1, 3, 1, 3, 3, 3, 3, 3,
	};
	uint16_t nbits = (uint16_t)(P25_STREAM_BYTES * 8u);

	for (uint16_t s = 0; (uint16_t)(s + 48u + P25_NID_REL + 66u) <= nbits; s++) {
		int m0 = 0, m2 = 0;

		for (uint8_t i = 0; i < 24u; i++) {
			uint8_t d = (uint8_t)((stream_bit((uint16_t)(s + 2u * i)) << 1) |
					      stream_bit((uint16_t)(s + 2u * i + 1u)));
			if (d != fsw[i]) {
				m0++;
			}
			if (((d + 2u) & 3u) != fsw[i]) {
				m2++;
			}
		}
		if (m0 > 4 && m2 > 4) {
			continue;
		}
		// The first sync in the stream is the one the chip aligned its block to: the NID's post-sync info starts
		// P25_NID_REL bits after the sync. A wide offset search would also land on a false codeword inside the
		// DMR-shaped gap the chip leaves between the sync and the P25 info (bench 2026-11-04)
		uint8_t rot = m0 <= m2 ? 0u : 2u;
		uint16_t n0 = (uint16_t)(s + 48u + P25_NID_REL);
		uint64_t nid = 0;

		for (uint8_t i = 0; i < 33u; i++) {
			if (i == 11u) {		// the P25 status symbol at NID dibit 11
				continue;
			}
			uint8_t d = (uint8_t)((stream_bit((uint16_t)(n0 + 2u * i)) << 1) |
					      stream_bit((uint16_t)(n0 + 2u * i + 1u)));
			d = (uint8_t)((d + rot) & 3u);
			nid = (nid << 2) | d;
		}
		uint16_t info = 0;
		int e = p25_bch_decode(nid >> 1, &info);

		if (e < 0) {
			return false;
		}
		uint8_t du = (uint8_t)(info & 0xFu);
		uint8_t flag = (uint8_t)(nid & 1u);
		uint8_t exp = (du == 5u || du == 0xAu) ? 1u : 0u;

		if (flag != exp) {
			return false;
		}
		*nac = (uint16_t)(info >> 4);
		*duid = du;
		*errs = (uint8_t)e;
		return true;
	}
	return false;
}

static uint8_t read_block(uint32_t *ty, uint32_t *ps, uint32_t *list, uint32_t *count, uint8_t *out, uint8_t max)
{
	uint32_t irq = hrc_irq_save();
	bool full = stream_len >= P25_STREAM_BYTES;

	*ty = type_info;
	*ps = phy_state;
	*list = last_list;
	*count = blocks;
	uint8_t n = (uint8_t)(stream_len < max ? stream_len : max);
	memcpy(out, stream, n);
	hrc_irq_restore(irq);

	if (full && on) {	// the stream only settles at a block boundary; decode from the stable buffer
		uint16_t nac = 0;
		uint8_t duid = 0, errs = 0;

		if (p25_nid_scan(&nac, &duid, &errs)) {
			dlog("p25p nid nac %03x duid %x err %u", (unsigned)nac, (unsigned)duid, (unsigned)errs);
		}
	}
	return n;
}

// The 8 kHz FM discriminator (modem-hrc7000.md 8.5): the modem writes 256-sample halves of its FM receive audio, and
// the system interrupt bit 14 says a half is ready. Keep one 0.5 s capture: the caller tunes the radio in FM and
// leaves it idle, then the board's audio_to_modem routes the RF chip's discriminator into the modem.
#define P25_AUDIO_SAMPLES	4096u

static int16_t p25_audio[P25_AUDIO_SAMPLES];
static volatile uint16_t p25_audio_len, p25_audio_rd;

static void audio_half(void)
{
	uint16_t len = p25_audio_len;

	if (len + HRC7000_FM_RX_SAMPLES > P25_AUDIO_SAMPLES) {
		hrc7000_fm_rx_audio_irq(modem, false);	// full: never leave the source running
		return;
	}
	if (hrc7000_fm_rx_audio(modem, &p25_audio[len]) != 0) {
		p25_audio_len = (uint16_t)(len + HRC7000_FM_RX_SAMPLES);
	}
}

static bool audio_start(void)
{
	if (modem == NULL || board.radio == NULL || board.radio->audio_to_modem == NULL || hrc_dmr_tx_active()) {
		return false;
	}
	p25_audio_len = p25_audio_rd = 0;
	// The board sets the FD6818's FM mode and RX state (0x47=0x0140, 0x30=0xBFF1) and routes its audio into the
	// modem (PTB15 low); the radio core left the C7000 in FM (WORK_MODE 0x80) on its frequency
	board.radio->audio_to_modem(true);
	if (!hrc_dmr_fm_rx_hook(audio_half)) {
		board.radio->audio_to_modem(false);
		return false;
	}
	hrc7000_fm_rx_audio_irq(modem, true);
	return true;
}

static void audio_stop(void)
{
	if (modem == NULL) {
		return;
	}
	hrc7000_fm_rx_audio_irq(modem, false);
	hrc_dmr_fm_rx_hook(NULL);
	if (board.radio != NULL && board.radio->audio_to_modem != NULL) {
		board.radio->audio_to_modem(false);
	}
}

static uint16_t audio_read(int16_t *buf, uint16_t max)
{
	uint16_t n = (uint16_t)(p25_audio_len - p25_audio_rd);

	if (n > max) {
		n = max;
	}
	if (n != 0) {
		memcpy(buf, &p25_audio[p25_audio_rd], (size_t)n * 2u);
		p25_audio_rd = (uint16_t)(p25_audio_rd + n);
	}
	return n;
}

const hal_p25_probe_t hrc_p25_probe = {
	.start = start,
	.start_ex = start_cfg,
	.stop = stop,
	.read = read_block,
	.audio_start = audio_start,
	.audio_stop = audio_stop,
	.audio_read = audio_read,
};

#endif
