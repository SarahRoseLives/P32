// SPDX-License-Identifier: GPL-3.0-or-later
#include "p25_demod.h"
#include <string.h>
#include "p25_demod_filter.h"
#include "p25_demod_math.h"
static const uint8_t sync_bytes[6]={0x55,0x75,0xf5,0xff,0x77,0xff};
void p25_demod_init(p25_demod *d) {
    memset(d,0,sizeof *d);
    for(unsigned i=0;i<10;++i) {d->lane[i].next_q16=(int32_t)(i+1)*65536;d->lane[i].period_q16=10*65536;}
}
void p25_demod_gap(p25_demod *d) {p25_demod_init(d);}
static bool acquire(p25_demod_lane *l) {
    static const int8_t sign[24]={1,1,1,1,1,-1,1,1,-1,-1,1,1,-1,-1,-1,-1,1,-1,1,-1,-1,-1,-1,-1};
    int32_t sx=l->sum,sxy=0;
    const int16_t *history=&l->history[l->index];
    for(unsigned i=0;i<24;++i)sxy+=sign[i]*history[i];
    sxy*=3;
    const int32_t sy=-6,yy=5148;
    int64_t xx=24*(int64_t)l->sum_squares-(int64_t)sx*sx;
    int32_t xy=24*sxy-sx*sy;
    if(xx<24000 || !yy) return false;
    int64_t numerator=(int64_t)xy*xy;
    int64_t scaled=numerator*1000,denominator=xx*yy;
    // floor(scaled/denominator) >= 900 iff scaled >= 900*denominator.
    // Most timing hypotheses fail sync: avoid software 64-bit division there.
    // With 24 int16 samples divided by 16, these products fit int64_t.
    if(scaled<900*denominator) return false;
    unsigned quality=(unsigned)(scaled/denominator);
    int32_t gain=(int32_t)((int64_t)xy*16/yy);
    if(gain>-100 && gain<100) return false;
    l->gain=gain;l->offset=(sx*16-gain*sy)/24;l->quality=quality;
    {
        uint32_t ag=(uint32_t)(gain<0 ? -gain : gain);
        l->gain_recip=ag!=0 ? (int32_t)(((1u<<16)+(ag>>1))/ag) : 0;
    }
    l->previous_symbol=0;
    p25_rx_gap(&l->rx);
    for(unsigned i=0;i<24;++i)
        p25_rx_push(&l->rx,(sync_bytes[i/4]>>(6-2*(i%4)))&3);
    return true;
}
static void symbol(p25_demod *d,p25_demod_lane *l,int16_t value) {
    int32_t old=l->history[l->index], fresh=value/16;
    l->sum+=fresh-old;l->sum_squares+=fresh*fresh-old*old;
    l->history[l->index]=l->history[l->index+24]=(int16_t)fresh;if(++l->index==24)l->index=0;
    if(l->filled<24) ++l->filled;
    // Do not reinterpret voice data as a sync while a validated unit is in flight.
    if(!l->rx.symbols && l->filled==24) {
        uint32_t start=d->clock_us ? d->clock_us() : 0;
        ++d->sync_tests;bool found=acquire(l);
        if(d->clock_us)d->sync_us+=d->clock_us()-start;
        if(found) {++d->acquisitions;return;}
    }
    if(!l->rx.symbols || !l->gain) return;
    int32_t x=value-l->offset,g=l->gain;
    if(g<0) {x=-x;g=-g;}
    uint8_t b=x< -2*g ? 3 : x<0 ? 2 : x<2*g ? 0 : 1;
    int32_t decision=b==3 ? -3 : b==2 ? -1 : b==0 ? 1 : 3;
    if(l->previous_symbol) {
        // Mueller and Muller timing error, normalized by the sync-derived gain.
        // Q16 loop gains 1/32 (phase), 1/4096 (rate), rate bounded to +/-2000 ppm.
        int32_t delta=l->previous_symbol*x-decision*l->previous_value;
        int32_t error=p25_timing_error(delta,g);
        l->period_q16+=error/4096;
        if(l->period_q16>656671)l->period_q16=656671;
        if(l->period_q16<654049)l->period_q16=654049;
        l->next_q16+=error/32;
    }
    l->previous_value=x;l->previous_symbol=decision;
    if(p25_rx_push(&l->rx,b)) {
        // Adjacent timing hypotheses complete the same unit within one symbol.
        // Prefer fewer FEC corrections, then the strongest sync fit. FEC cannot
        // prove integrity beyond its correction radius. Emit only one unit.
        // A zero-error winner cannot be beaten by a weaker sync fit.
        // Identical frame bytes also have identical FEC scores.
        if(d->pending_ticks && ((d->pending_errors==0 && l->quality<=d->pending_quality)
            || (l->rx.duid==d->pending.duid && !memcmp(l->rx.frame,d->pending.frame,sizeof l->rx.frame)))) {
            ++d->fec_skipped;
            if(l->quality>d->pending_quality) {d->pending=l->rx;d->pending_quality=l->quality;}
            return;
        }
        uint32_t fec_start=d->clock_us ? d->clock_us() : 0;
        ++d->fec_candidates;
        unsigned errors=0;
        if(l->rx.duid==5 || l->rx.duid==10) {
            for(unsigned i=0;i<9;++i) {
                uint8_t payload[11];unsigned corrected;
                if(p25_rx_voice(l->rx.frame,i,payload,&corrected))errors+=corrected;
                // Corrections cannot decrease as more codewords are scored.
                // Once this candidate exceeds the pending winner it cannot
                // win, even with a stronger sync fit. Equal scores must keep
                // running because the sync-quality tie-break still matters.
                if(d->pending_ticks && errors>d->pending_errors)break;
            }
        }
        if(d->clock_us)d->fec_us+=d->clock_us()-fec_start;
        if(!d->pending_ticks) {d->pending_ticks=11;d->pending_errors=~0u;}
        if(errors<d->pending_errors || (errors==d->pending_errors && l->quality>d->pending_quality)) {
            d->pending=l->rx;d->pending_quality=l->quality;d->pending_errors=errors;
        }
    }
}
static void hunting_history(p25_demod *d,int16_t value) {
    d->hunt_history[d->hunt_index]=value/16;
    if(++d->hunt_index==240)d->hunt_index=0;
    if(d->hunt_filled<240)++d->hunt_filled;
}
static void hunting_rearm(p25_demod *d) {
    // Independent learned clocks can converge during a call and then leave
    // no useful sampling-phase diversity for the next transmitter. Rearm
    // every 600 ms without an emitted unit, including long malformed hunts.
    // False sync/NID attempts must not refresh this deadline.
    if(d->idle_ticks<28800u)++d->idle_ticks;
    if(d->idle_ticks<28800u || d->hunt_filled<240 || d->pending_ticks)return;
    for(unsigned i=0;i<10;++i)if(d->lane[i].rx.symbols)return;
    uint32_t (*hunt_clock)(void)=d->hunt_clock_us ? d->hunt_clock_us : d->clock_us;
    uint32_t started=hunt_clock ? hunt_clock() : 0;
    unsigned phase=(unsigned)(d->ticks%10);
    for(unsigned i=0;i<10;++i) {
        p25_demod_lane *l=&d->lane[i];
        unsigned age=(phase+10-((i+1)%10))%10;
        unsigned oldest=d->hunt_index+9-age;
        l->sum=l->sum_squares=0;
        for(unsigned j=0;j<24;++j) {
            unsigned pos=oldest+10*j;if(pos>=240)pos-=240;
            int32_t sample=d->hunt_history[pos];
            l->history[j]=l->history[j+24]=(int16_t)sample;
            l->sum+=sample;l->sum_squares+=sample*sample;
        }
        l->index=0;l->filled=24;l->quality=0;l->offset=l->gain=0;
        l->previous_value=l->previous_symbol=0;
        l->period_q16=10*65536;
        l->next_q16=(int32_t)(10-age)*65536;
        p25_rx_gap(&l->rx); // Keep per-lane telemetry counters monotonic.
        // The last sample for this nominal phase is zero..nine ticks old.
        // Rebuild rather than erase a partial sync; also recognize a sync
        // already complete at that sample, without skipping its next dibit.
        uint32_t start=d->clock_us ? d->clock_us() : 0;
        ++d->sync_tests;bool found=acquire(l);
        if(d->clock_us)d->sync_us+=d->clock_us()-start;
        if(found)++d->acquisitions;
    }
    d->idle_ticks=0;++d->hunt_rearms;
    if(hunt_clock) {
        uint32_t elapsed=hunt_clock()-started;
        d->hunt_us+=elapsed;
        if(elapsed>d->hunt_max_us)d->hunt_max_us=elapsed;
    }
}
static void interpolated(p25_demod *d,int16_t value,p25_demod_frame_fn emit,void *context) {
    if(d->pending_ticks && !--d->pending_ticks) {
        ++d->frames;emit(&d->pending,context);d->pending_quality=0;d->idle_ticks=0;
    }
    ++d->ticks;hunting_history(d,value);
    for(unsigned i=0;i<10;++i) {
        p25_demod_lane *l=&d->lane[i];
        l->next_q16-=65536;
        if(l->next_q16<=0) {
            uint32_t back=(uint32_t)(-l->next_q16);
            int32_t x=p25_interpolate(d->previous,value,back);
            l->next_q16+=l->period_q16;
            uint32_t start=d->clock_us ? d->clock_us() : 0;
            symbol(d,l,(int16_t)x);
            if(d->clock_us)d->symbol_us+=d->clock_us()-start;
        }
    }
    // Run after this tick's data decisions: newly reconstructed lanes are
    // scheduled relative to now and must not consume the current tick twice.
    hunting_rearm(d);d->previous=value;
}
void p25_demod_push(p25_demod *d,int16_t sample,p25_demod_frame_fn emit,void *context) {
    uint32_t start=d->clock_us ? d->clock_us() : 0;
    d->fir[d->fir_index]=d->fir[d->fir_index+21]=sample;
    int16_t values[6];
    for(unsigned p=0;p<6;++p) {
        int64_t sum=0;
        const int16_t *input=&d->fir[d->fir_index+21];
        // Duplicated ring removes modulo from every FIR tap. Products fit
        // int32; full-scale phase sums still require an int64 accumulator.
        for(unsigned k=p;k<121;k+=6)
            sum+=p25_interp[k]*(int32_t)*input--;
        int32_t x=(int32_t)(sum/32768);
        if(x>32767)x=32767;
        if(x< -32768)x=-32768;
        values[p]=(int16_t)x;
    }
    if(++d->fir_index==21)d->fir_index=0;
    if(d->clock_us)d->fir_us+=d->clock_us()-start;
    for(unsigned p=0;p<6;++p)interpolated(d,values[p],emit,context);
}
