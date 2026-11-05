// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef P25_RECEIVE_POLICY_H
#define P25_RECEIVE_POLICY_H
#include "p25_call.h"
#include <string.h>

// Selection (NAC/PROM) happens before this policy. PROM never bypasses metadata
// integrity or clear/group qualification. No identity or ESS crosses a boundary.
typedef enum {
    P25_RX_WAIT_METADATA, P25_RX_CLEAR, P25_RX_ENCRYPTED,
    P25_RX_UNSUPPORTED, P25_RX_BAD_METADATA
} p25_receive_class;
typedef struct {
    uint32_t source,last_unit_us;
    uint16_t nac,group,keyid;
    uint8_t algid,mfid,last_duid;
    bool active,ess_valid,lc_valid,group_valid,source_valid;
    bool header_unsupported,lc_encrypted,lc_unsupported;
    p25_receive_class state;
} p25_receive_policy;
typedef struct {bool reset,new_call,voice,end,discard;} p25_receive_event;

static inline void p25_receive_reset(p25_receive_policy *s) {memset(s,0,sizeof *s);}
static inline const char *p25_receive_label(const p25_receive_policy *s) {
    return s->state==P25_RX_CLEAR ? "CLEAR GROUP" : s->state==P25_RX_ENCRYPTED ? "ENCRYPTED / MUTED"
        : s->state==P25_RX_UNSUPPORTED ? "UNSUPPORTED / MUTED"
        : s->state==P25_RX_BAD_METADATA ? "BAD METADATA / MUTED" : "WAIT CALL METADATA";
}
static inline p25_receive_event p25_receive_update(p25_receive_policy *s,uint16_t nac,
        uint8_t duid,const p25_call_metadata *m,bool valid,uint32_t now) {
    p25_receive_event event={false,false,false,false,false};
    if(duid==3 || duid==15) {
        // A foreign NAC's terminator cannot end the selected call in PROM.
        if(s->active && s->nac==nac) {p25_receive_reset(s);event.end=true;}
        return event;
    }
    if(duid!=0 && duid!=5 && duid!=10)return event;
    bool fresh=!s->active || s->nac!=nac || now-s->last_unit_us>600000u || duid==0;
    if(fresh) {p25_receive_reset(s);s->active=true;s->nac=nac;event.reset=event.new_call=event.discard=true;}
    p25_receive_class previous=s->state;
    bool prior_clear_ess=s->ess_valid && s->algid==0x80;
    s->last_unit_us=now;
    if(duid==0) {
        if(valid && m->ess_valid && m->group_valid && m->mfid_valid) {
            s->ess_valid=true;s->algid=m->algid;s->keyid=m->keyid;
            s->group=m->group;s->group_valid=true;s->mfid=m->mfid;
            s->header_unsupported=m->mfid!=0 || (m->classification!=P25_CALL_CLEAR && m->classification!=P25_CALL_ENCRYPTED);
        } else s->state=P25_RX_BAD_METADATA;
    } else if(duid==5) {
        if(!valid || !m->lc_valid) {
            s->lc_valid=s->source_valid=s->group_valid=false;s->source=0;s->group=0;s->state=P25_RX_BAD_METADATA;
        } else {
            bool changed=(s->group_valid && (!m->group_valid || s->group!=m->group))
                || (s->source_valid && (!m->source_valid || s->source!=m->source));
            // A different call without HDU, or a missing ESS unit, cannot inherit
            // a previous clear authorization. Wait for fresh ESS before audio.
            if(changed || (!fresh && s->last_duid==5)) {
                s->ess_valid=false;s->algid=0;s->keyid=0;event.reset=event.discard=true;
                if(changed) {event.new_call=true;s->header_unsupported=false;}
            }
            s->lc_valid=true;s->group_valid=m->group_valid;s->source_valid=m->source_valid;
            s->group=m->group;s->source=m->source;s->mfid=m->mfid;
            s->lc_encrypted=m->classification==P25_CALL_ENCRYPTED || m->protected_flag || (m->service_options&0x40);
            s->lc_unsupported=(m->classification!=P25_CALL_CLEAR && m->classification!=P25_CALL_ENCRYPTED)
                || (m->classification==P25_CALL_CLEAR && (!m->mfid_valid || m->mfid || m->lco
                    || m->protected_flag || m->standard_format || m->lc_options || (m->service_options&0x30)
                    || !m->group_valid || !m->source_valid));
        }
    } else {
        if(!valid || !m->ess_valid || (m->classification!=P25_CALL_CLEAR && m->classification!=P25_CALL_ENCRYPTED)) {
            s->ess_valid=false;s->algid=0;s->keyid=0;s->state=P25_RX_BAD_METADATA;
        } else {
            if(!fresh && s->last_duid==10) {
                s->lc_valid=s->source_valid=s->group_valid=false;s->source=0;s->group=0;event.reset=event.discard=true;
            }
            s->ess_valid=true;s->algid=m->algid;s->keyid=m->keyid;
        }
    }
    // Compute classification from independently validated ESS and call type.
    // Bad metadata is shown until the missing portion is recovered.
    if(s->header_unsupported || s->lc_unsupported)s->state=P25_RX_UNSUPPORTED;
    else if((s->ess_valid && s->algid!=0x80) || s->lc_encrypted)s->state=P25_RX_ENCRYPTED;
    else if(s->ess_valid && s->lc_valid && s->group_valid && s->source_valid)s->state=P25_RX_CLEAR;
    else if(s->state!=P25_RX_BAD_METADATA)s->state=P25_RX_WAIT_METADATA;
    if((previous==P25_RX_CLEAR && s->state!=P25_RX_CLEAR)
        || s->state==P25_RX_ENCRYPTED || s->state==P25_RX_UNSUPPORTED || s->state==P25_RX_BAD_METADATA)event.discard=true;
    if(previous!=P25_RX_CLEAR && s->state==P25_RX_CLEAR)event.reset=true;
    s->last_duid=duid;
    // LDU2 carries ESS for the next voice pair. A newly clear ESS must not
    // authorize this LDU2 under an unknown/encrypted previous crypto state.
    // A newly encrypted ESS mutes immediately, before any PCM is synthesized.
    event.voice=duid!=0 && s->state==P25_RX_CLEAR && (duid!=10 || prior_clear_ess);
    return event;
}
#endif
