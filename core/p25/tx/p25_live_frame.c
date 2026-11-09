// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 The RadioFoundry Authors
// Ported from DM32-Multimode for P32 conventional P25 TX.
// Fixed-storage adaptation of pinned OP25 imbe_unpack/header_encode/interleave.
// Reference and provenance: research/references/boatbod/op25, UPSTREAMS.json.
#include "p25_live_frame.h"
#include "p25_interleave.h"
#include "p25_test_vectors.h"
#include "p25_channel_masks.h"
#include <string.h>
_Static_assert(sizeof(p25_channel_frames)==549,"Template byte layout changed");
bool p25_channel_build(p25_channel_frames *out,uint32_t nac,uint32_t tg,uint32_t source) {
    if(!out || nac>0xfff || tg>0xffff || source>0xffffff) return false;
    memcpy(out->hdu,p25_test_hdu,99); memcpy(out->ldu1,p25_test_ldu1,216);
    memcpy(out->ldu2,p25_test_ldu2,216); memcpy(out->tdu,p25_test_tdu,18);
    const uint32_t values[]={nac^0x293u,tg^1u,source^1u};
    const unsigned widths[]={12,16,24};
    unsigned index=0;
    unsigned char *bytes=(unsigned char *)out;
    for(unsigned field=0;field<3;++field) for(unsigned bit=0;bit<widths[field];++bit,++index) {
        if(!(values[field]&(1u<<bit))) continue;
        for(unsigned j=p25_channel_index[index];j<p25_channel_index[index+1];++j) {
            uint32_t patch=p25_channel_masks[j]; bytes[patch>>8]^=(uint8_t)patch;
        }
    }
    return true;
}
static uint32_t golay(uint32_t u) {
    static const uint32_t g[12]={040006165,020003073,010007550,04003664,02001732,01006631,
        0403315,0201547,0106706,045227,024476,014353};
    uint32_t v=0;
    for(unsigned i=0;i<12;++i) if(u&(1u<<(11-i))) v^=g[i];
    return v>>1;
}
static uint32_t hamming(uint32_t u) {
    static const uint16_t g[11]={0x400f,0x200e,0x100d,0x080c,0x040b,0x020a,0x0109,0x0087,0x0046,0x0025,0x0013};
    uint32_t v=0;
    for(unsigned i=0;i<11;++i) if(u&(1u<<(10-i))) v^=g[i];
    return v;
}
static uint32_t pn_bits(uint32_t *pn,unsigned bits) {
    uint32_t value=0;
    for(unsigned i=0;i<bits;++i) {
        *pn=(173u**pn+13849u)&0xffffu;
        value=(value<<1)|(*pn>>15);
    }
    return value;
}
void p25_live_ldu(uint8_t out[216],const uint8_t payload[9][11],bool ldu2) {
    p25_live_ldu_template(out,payload,ldu2 ? p25_test_ldu2 : p25_test_ldu1);
}
void p25_live_ldu_template(uint8_t out[216],const uint8_t payload[9][11],const uint8_t base[216]) {
    memcpy(out,base,216);
    for(unsigned f=0;f<9;++f) {
        const uint8_t *p=payload[f];
        uint32_t u[8]={ (p[0]<<4)|(p[1]>>4), ((p[1]&15)<<8)|p[2],
            (p[3]<<4)|(p[4]>>4), ((p[4]&15)<<8)|p[5],
            (p[6]<<3)|(p[7]>>5), ((p[7]&31)<<6)|(p[8]>>2),
            ((p[8]&3)<<9)|(p[9]<<1)|(p[10]>>7), p[10]&127 };
        uint32_t pn=u[0]<<4;
        unsigned index=0;
        for(unsigned j=0;j<8;++j) {
            unsigned bits=j<4 ? 23 : j<7 ? 15 : 7;
            uint32_t value=j<4 ? golay(u[j]) : j<7 ? hamming(u[j]) : u[j];
            if(j>0 && j<7) value^=pn_bits(&pn,bits);
            for(unsigned b=0;b<bits;++b) {
                unsigned bit=p25_voice_bits[f][index++];
                uint8_t mask=(uint8_t)(1u<<(7-bit%8));
                out[bit/8]=(uint8_t)((out[bit/8]&~mask)|(((value>>(bits-1-b))&1u) ? mask : 0));
            }
        }
    }
}
