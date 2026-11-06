// SPDX-License-Identifier: MIT
// Copyright (c) 2026 The RadioFoundry Authors
// HR_C7000 P25 physical-layer receive (docs/spec/p25.md 5.1, 8; modem-hrc7000.md 9.6). The chip's PHY receive test
// mode is documented for DMR bit-error tests: WORK_MODE 0x02 (continuous), RF_MODE 0xA0000060 (bit 31 and 29 set,
// two-point transmit, IF receive), LAYER2_CONTROL 0x40, LAYER2_TXRX_CTRL 0x41, then the deframed information in the RX
// RAM at +0x30 after the chip finds a sync. The P25 frame sync is 48 bits, the same width as DMR's, so the probe puts
// it in the receive-sync registers (and three byte-order variants in the others) and undoes the chip's slot order to
// assemble the on-air stream. Two callers: the debug link's bench probe (raw block, the 8 kHz discriminator capture)
// and the control-channel monitor, which reads the decoded CC through last_cc.
#include "config.h"
#if CONFIG_P25 || CONFIG_P25_PROBE || CONFIG_P25_VOICE

#include <string.h>
#include "platform.h"
#include "board.h"
#include "p25_probe.h"
#include "dmr_glue.h"
#include "debuglink/dlog.h"
#include "p25/p25_bch.h"
#include "p25/p25_tsbk.h"
#include "p25/p25_tsbk_parse.h"

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
// One PHY_RX block is a 30 ms slot: CACH/slot type at 0x30, the 264 deframed bits at 0x33. Keep 7 slots (252 bytes)
// and undo the chip's DMR storage order to get the on-air P25 stream (docs/spec/p25.md 7.2).
#define P25_BLOCK_BYTES		36u	// one 288-bit slot
#define P25_STREAM_BLOCKS	7u
#define P25_STREAM_BYTES	(P25_BLOCK_BYTES * P25_STREAM_BLOCKS)
static uint8_t stream[P25_STREAM_BYTES];
static uint16_t stream_len;
// The monitor screen's last decoded control channel; last_cc() runs the decode in task context (the trellis is too
// heavy for the interrupt) and then clears the capture so the next burst fills it again
static hal_p25_cc_t last;
static p25_cc_t band;	// accumulated IdenUp band plans (the capture decodes one TSBK at a time)
static uint32_t decoded_blocks, frames;

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
		uint16_t off = (uint16_t)(C7_RX_RAM + RX_INFO_OFF);
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
	type_info = phy_state = last_list = 0;
	memset(&last, 0, sizeof last);
	decoded_blocks = 0;
	frames = 0;
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

// The chip stores a slot as A (CACH/slot type), B (pre-sync data), F (sync or embedded signalling), then P
// (post-sync data); the on-air order is A B F P. Undo the DMR storage order so the P25 frame is contiguous.
static uint16_t onair_source(uint16_t bit)
{
	uint16_t slot = (uint16_t)(bit / 288u);
	uint16_t k = (uint16_t)(bit % 288u);
	uint16_t src;

	if (k < 44u) {
		src = k;
	} else if (k < 132u) {
		src = (uint16_t)(92u + (k - 44u));
	} else if (k < 180u) {
		src = (uint16_t)(44u + (k - 132u));
	} else {
		src = (uint16_t)(180u + (k - 180u));
	}
	return (uint16_t)(slot * 288u + src);
}

static uint8_t onair_bit(uint16_t bit)
{
	return stream_bit(onair_source(bit));
}

// Find the P25 frame sync in the un-reordered stream, read the NID at the on-air +48 and then the TSBK after it. The
// A and F fields carry the chip's DMR decoding, so the TSBK may still need those positions treated as erasures
// (p25.md 7.2); the CRC decides.
static bool p25_decode(uint16_t *nac, uint8_t *duid, bool *have_tsbk, p25_cc_t *cc)
{
	static const uint8_t fsw[24] = {
		1, 1, 1, 1, 1, 3, 1, 1, 3, 3, 1, 1, 3, 3, 3, 3, 1, 3, 1, 3, 3, 3, 3, 3,
	};
	uint16_t nbits = (uint16_t)((stream_len / P25_BLOCK_BYTES) * 288u);
	bool got_nid = false;

	*have_tsbk = false;

	// The P25 frame (37.5 ms) is longer than the chip's 30 ms slot, so each frame sync lands at a different place in
	// the slot; the A/F fields (the chip's DMR decoding) overlap the TSBK by a different amount at each. Try every
	// sync, not just the first, and take the TSBK from whichever one the erasure trellis can finish.
	for (uint16_t s = 0; (uint16_t)(s + 48u + 66u) <= nbits; s++) {
		int m0 = 0, m2 = 0;

		for (uint8_t i = 0; i < 24u; i++) {
			uint8_t d = (uint8_t)((onair_bit((uint16_t)(s + 2u * i)) << 1) |
					      onair_bit((uint16_t)(s + 2u * i + 1u)));
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
		uint8_t rot = m0 <= m2 ? 0u : 2u;
		uint64_t nid = 0;

		for (uint8_t i = 0; i < 33u; i++) {
			if (i == 11u) {		// the status symbol at NID dibit 11
				continue;
			}
			uint8_t d = (uint8_t)((onair_bit((uint16_t)(s + 48u + 2u * i)) << 1) |
					      onair_bit((uint16_t)(s + 48u + 2u * i + 1u)));
			d = (uint8_t)((d + rot) & 3u);
			nid = (nid << 2) | d;
		}
		uint16_t info = 0;
		int e = p25_bch_decode(nid >> 1, &info);

		if (e < 0) {
			continue;
		}
		uint8_t du = (uint8_t)(info & 0xFu);
		uint8_t exp = (du == 5u || du == 0xAu) ? 1u : 0u;

		if ((uint8_t)(nid & 1u) != exp) {
			continue;
		}
		if (!got_nid) {
			*nac = (uint16_t)(info >> 4);
			*duid = du;
			got_nid = true;
		}
		if (*have_tsbk) {
			continue;
		}

		// The TSBK starts after the NID's 33 on-air dibits and its data dibits skip the status symbol every 36. The
		// chip's DMR deframer rewrites a slot's A (CACH/slot type) and F (embedded signalling) fields, so the TSBK
		// dibits that land there are unknown; mark them erased for the trellis (p25.md 7.2)
		uint8_t tb[196];
		uint8_t er[P25_TSBK_DIBITS] = { 0 };
		uint16_t n = 0, g = 57;		// dibits from the sync: 24 (sync) + 33 (NID)

		while (n < 196u && (uint16_t)(s + 2u * g + 1u) < nbits) {
			if (!(g >= 35u && ((g - 35u) % 36u) == 0u)) {
				uint8_t d = (uint8_t)((onair_bit((uint16_t)(s + 2u * g)) << 1) |
						      onair_bit((uint16_t)(s + 2u * g + 1u)));
				uint16_t pos = (uint16_t)((s + 2u * g) % 288u);

				d = (uint8_t)((d + rot) & 3u);
				if (pos < 44u || (pos >= 132u && pos < 180u)) {
					er[n / 2u] = 1u;
				}
				tb[n++] = (uint8_t)((d >> 1) & 1u);
				tb[n++] = (uint8_t)(d & 1u);
			}
			g++;
		}
		if (n == 196u) {
			uint8_t out[12];

			if (p25_tsbk_decode(tb, er, out) >= 0) {
				p25_cc_t tmp = band;	// keep the band plans from earlier captures

				tmp.nac = *nac;
				(void)p25_tsbk_parse(out, &tmp);	// opcode always set; fields when known
				band = tmp;
				*cc = tmp;
				*have_tsbk = true;
			}
		}
	}
	return got_nid;
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

	if (full && on) {
		uint16_t nac = 0;
		uint8_t duid = 0;
		bool have = false;
		p25_cc_t cc;

		memset(&cc, 0, sizeof cc);
		if (p25_decode(&nac, &duid, &have, &cc)) {
			dlog("p25p nid nac %03x duid %x", (unsigned)nac, (unsigned)duid);
			if (have) {
				dlog("p25p cc op %02x nac %03x wacn %05x sys %03x rfss %u site %u tg %u vch %u f %u",
				     (unsigned)cc.opcode, (unsigned)cc.nac, (unsigned)cc.wacn, (unsigned)cc.sys,
				     (unsigned)cc.rfss, (unsigned)cc.site, (unsigned)cc.tg, (unsigned)cc.vch,
				     (unsigned)cc.freq_hz);
			}
		}
	}
	return n;
}

#if CONFIG_P25_PROBE || CONFIG_P25_VOICE
// The 8 kHz FM discriminator (modem-hrc7000.md 8.5): the modem writes 256-sample halves of its FM receive audio, and
// the system interrupt bit 14 says a half is ready. A ring of four halves (128 ms) so a software demodulator can keep
// up; the p25audio command drains it too. The caller tunes the radio and leaves it idle, then the board's
// audio_to_modem routes the RF chip's discriminator into the modem.
#define P25_AUDIO_HALVES	8u
#define P25_AUDIO_SAMPLES	(P25_AUDIO_HALVES * HRC7000_FM_RX_SAMPLES)

static int16_t p25_audio[P25_AUDIO_SAMPLES];
static volatile uint16_t p25_audio_head, p25_audio_tail, p25_audio_dropped;
static uint32_t p25_saved_fm_bw, p25_saved_fm_dev;
static bool p25_saved_valid;

static void audio_half(void)
{
	uint16_t head = p25_audio_head;

	if ((uint16_t)(head - p25_audio_tail) > P25_AUDIO_SAMPLES - HRC7000_FM_RX_SAMPLES) {
		p25_audio_dropped++;	// the reader is behind: keep what is there, lose this half
		return;
	}
	if (hrc7000_fm_rx_audio(modem, &p25_audio[head & (P25_AUDIO_SAMPLES - 1u)]) != 0) {
		p25_audio_head = (uint16_t)(head + HRC7000_FM_RX_SAMPLES);
	}
}

static bool audio_start(void)
{
	if (modem == NULL || board.radio == NULL || board.radio->audio_to_modem == NULL || hrc_dmr_tx_active()) {
		return false;
	}
	p25_audio_head = p25_audio_tail = p25_audio_dropped = 0;
	// The board routes the RF chip's discriminator into the modem (PTB15 low); the radio core left the C7000 in FM
	// (WORK_MODE 0x80) on its frequency. Prefer the data (DMR) receive table: its discriminator filter is wide enough
	// for the 4FSK, where the FM voice table is not
	if (board.radio->audio_to_modem_data != NULL) {
		board.radio->audio_to_modem_data(true);
	} else {
		board.radio->audio_to_modem(true);
	}
	// The C7000's FM receive path for voice low-passes the discriminator (C7_FM_BANDWIDTH bit 6), which smears
	// 4FSK. The software demodulator needs the raw discriminator: FM receive, analog speaker off, bit 6 clear
	p25_saved_fm_bw = hrc7000_rd(modem, C7_FM_BANDWIDTH);
	p25_saved_fm_dev = hrc7000_rd(modem, C7_FM_DEV_COEF);
	p25_saved_valid = true;
	hrc7000_fm_rx(modem, false);
	hrc7000_audio_bits(modem, 0, C7_AUDIO_FM_PLAY);
	hrc7000_wr(modem, C7_FM_BANDWIDTH, hrc7000_rd(modem, C7_FM_BANDWIDTH) & ~(1u << 6));
	if (!hrc_dmr_fm_rx_hook(audio_half)) {
		if (board.radio->audio_to_modem_data != NULL) {
			board.radio->audio_to_modem_data(false);
		} else {
			board.radio->audio_to_modem(false);
		}
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
	if (p25_saved_valid) {
		hrc7000_wr(modem, C7_FM_BANDWIDTH, p25_saved_fm_bw);
		hrc7000_wr(modem, C7_FM_DEV_COEF, p25_saved_fm_dev);
		p25_saved_valid = false;
	}
	if (board.radio != NULL && board.radio->audio_to_modem_data != NULL) {
		board.radio->audio_to_modem_data(false);
	} else if (board.radio != NULL && board.radio->audio_to_modem != NULL) {
		board.radio->audio_to_modem(false);
	}
}

static uint16_t audio_read(int16_t *buf, uint16_t max)
{
	uint16_t tail = p25_audio_tail, n = (uint16_t)(p25_audio_head - tail);

	if (n > max) {
		n = max;
	}
	for (uint16_t i = 0; i < n; i++) {
		buf[i] = p25_audio[(uint16_t)(tail + i) & (P25_AUDIO_SAMPLES - 1u)];
	}
	p25_audio_tail = (uint16_t)(tail + n);
	return n;
}

#endif

// The monitor's last decoded control channel. Task context only: p25_decode scans the capture and runs the trellis,
// too heavy for the interrupt. A full capture is decoded once, then cleared so the next burst fills it; the 32-block
// cap (probe_isr) is re-armed. Fields a burst does not set keep their last value.
static bool last_cc(hal_p25_cc_t *out)
{
	uint32_t irq = hrc_irq_save();
	bool full = on && stream_len >= P25_STREAM_BYTES;
	uint32_t b = blocks;

	hrc_irq_restore(irq);

	if (full && b != decoded_blocks) {
		uint16_t nac = 0;
		uint8_t duid = 0;
		bool have = false;
		p25_cc_t cc;

		memset(&cc, 0, sizeof cc);
		if (p25_decode(&nac, &duid, &have, &cc)) {
			last.valid = true;
			last.nac = nac;
			last.duid = duid;
			if (have) {
				last.opcode = cc.opcode;
				if (cc.wacn != 0) {
					last.wacn = cc.wacn;
				}
				if (cc.sys != 0) {
					last.sys = cc.sys;
				}
				if (cc.rfss != 0) {
					last.rfss = cc.rfss;
				}
				if (cc.site != 0) {
					last.site = cc.site;
				}
				if (cc.tg != 0) {
					last.tg = cc.tg;
				}
				if (cc.vch != 0) {
					last.vch = cc.vch;
				}
				if (cc.freq_hz != 0) {
					last.freq_hz = cc.freq_hz;
				}
				last.voice_hz = cc.voice_hz;
				last.grants = cc.grant_seq;
			}
			frames++;
		}
		irq = hrc_irq_save();
		stream_len = 0;
		blocks = 0;
		decoded_blocks = 0;
		if (modem != NULL) {
			hrc7000_wr(modem, C7_SYS_INTERP_MASK, saved.sys_mask | PHY_BITS);
		}
		hrc_irq_restore(irq);
	}
	*out = last;
	out->blocks = frames;
	return last.valid;
}

const hal_p25_probe_t hrc_p25_probe = {
	.start = start,
	.start_ex = start_cfg,
	.stop = stop,
	.read = read_block,
#if CONFIG_P25_PROBE || CONFIG_P25_VOICE
	.audio_start = audio_start,
	.audio_stop = audio_stop,
	.audio_read = audio_read,
#endif
	.last_cc = last_cc,
};

#endif
