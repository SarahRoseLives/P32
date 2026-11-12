// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 The RadioFoundry Authors
// Ported from a GPL-3.0-or-later P25 reference implementation for conventional P25 TX.
#include "p25_live_tx.h"
#include "p25_test_vectors.h"
#include <string.h>
_Static_assert(LIVE_STARTUP_BASE_BYTES==P25_TEST_PREAMBLE_BYTES,"Startup fill reference changed");
void p25_live_tx_init(p25_live_tx *s) {
    memset(s,0,sizeof *s);
    memcpy(s->hdu,p25_test_hdu,99); memcpy(s->tdu,p25_test_tdu,18);
}
bool p25_live_tx_set_startup_extra(p25_live_tx *s,unsigned extra) {
    if(s->timing.state!=P25_TX_IDLE || extra>LIVE_STARTUP_EXTRA_MAX_BYTES) return false;
    s->startup_extra=(uint8_t)extra; return true;
}
bool p25_live_tx_push(p25_live_tx *s,const uint8_t ldu[216]) {
    if(s->finished || s->count==LIVE_BANKS || s->timing.state==P25_TX_ABORTED) return false;
    memcpy(s->banks[(s->head+s->count)%LIVE_BANKS],ldu,216);
    ++s->published; ++s->count;
    if(s->count>s->high) s->high=s->count;
    return true;
}
bool p25_live_tx_finish(p25_live_tx *s) {
    if(!s->published || s->published%2u) return false;
    s->finished=true; return true;
}
bool p25_live_tx_start(p25_live_tx *s,uint32_t now) {
    if(s->count<2 || s->timing.state!=P25_TX_IDLE) return false;
    return p25_test_tx_start(&s->timing,now,true);
}
static bool next_byte(p25_live_tx *s,uint8_t *out) {
    unsigned preamble_bytes=P25_TEST_PREAMBLE_BYTES+s->startup_extra;
    if(s->header_pos<preamble_bytes) {
        ++s->header_pos; *out=0x77; return true;
    }
    unsigned h=s->header_pos-preamble_bytes;
    if(h<sizeof p25_test_hdu) {
        ++s->header_pos; *out=s->hdu[h]; return true;
    }
    if(s->count) {
        *out=s->banks[s->head][s->pos++];
        if(s->pos==216) { s->pos=0; s->head=(s->head+1)%LIVE_BANKS; --s->count; ++s->consumed; }
        return true;
    }
    if(!s->finished) return false;
    *out=s->terminator_pos<sizeof s->tdu ? s->tdu[s->terminator_pos++] : 0x77;
    return true;
}
bool p25_live_tx_boundary(p25_live_tx *s,uint32_t now,uint8_t block[36]) {
    p25_test_tx *t=&s->timing;
    if(t->state!=P25_TX_WAITING && t->state!=P25_TX_STREAMING) return false;
    p25_test_tx_poll_limit(t,now,true,LIVE_RF_MAX_US);
    if(!p25_test_tx_active(t)) return false;
    if(t->have_boundary) {
        uint32_t gap=now-t->last_boundary_us;
        if(gap<15000u) { ++t->duplicate_irqs; return false; }
        if(gap>t->max_gap_us) t->max_gap_us=gap;
        if(gap>P25_TX_LATE_US) { ++t->late_irqs; p25_test_tx_abort(t,P25_TX_LATE); return false; }
    }
    t->last_boundary_us=now; t->have_boundary=true; t->state=P25_TX_STREAMING;
    for(unsigned i=0;i<36;++i) {
        if(!next_byte(s,&block[i])) {
            ++s->underruns; p25_test_tx_abort(t,P25_TX_STALLED); return false;
        }
        ++t->offset;
    }
    ++t->blocks;
    if(s->finished && !s->count && s->terminator_pos==sizeof p25_test_tdu) {
        t->state=P25_TX_DRAINING; t->drain_until_us=now+P25_TX_DRAIN_US;
    }
    return true;
}
