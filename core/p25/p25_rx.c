// SPDX-License-Identifier: GPL-3.0-or-later
// Pinned OP25 coding/interleave conventions; see references/UPSTREAMS.json.
#include "p25_rx.h"
#include "p25_interleave.h"
#include <string.h>
#include "p25_rx_golay.h"
static uint32_t golay(uint32_t u) {
    static const uint32_t table[3][16]={{0x0,0xc75,0x149f,0x18ea,0x254b,0x293e,0x31d4,0x3da1,0x46e3,0x4a96,0x527c,0x5e09,0x63a8,0x6fdd,0x7737,0x7b42},{0x0,0x81b3,0x10366,0x182d5,0x206cc,0x2877f,0x305aa,0x38419,0x401ed,0x4805e,0x5028b,0x58338,0x60721,0x68692,0x70447,0x785f4},{0x0,0x803da,0x1007b4,0x18046e,0x20031d,0x2800c7,0x3004a9,0x380773,0x40063a,0x4805e0,0x50018e,0x580254,0x600527,0x6806fd,0x700293,0x780149}};
    return table[0][u&15]^table[1][(u>>4)&15]^table[2][(u>>8)&15];
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

static unsigned weight(uint32_t x) { unsigned n=0; while(x) {x&=x-1;++n;} return n; }
static unsigned bit(const uint8_t *p,unsigned i) {return (p[i/8]>>(7-i%8))&1;}
static uint64_t nid_encode(uint16_t u) {
 static const uint64_t g[16]={
        0x8000cd930bdd3b2a, 0x4000ab5a8e33a6be,
        0x2000983e4cc4e874, 0x10004c1f2662743a,
        0x0800eb9c98ec0136, 0x0400b85d47ab3bb0,
        0x02005c2ea3d59dd8, 0x01002e1751eaceec,
        0x0080170ba8f56776, 0x0040c616dfa78890,
        0x0020630b6fd3c448, 0x00103185b7e9e224,
        0x000818c2dbf4f112, 0x0004c1f2662743a2,
        0x0002ad6a38ce9afb, 0x00019b2617ba7657};
 uint64_t v=0; for(unsigned i=0;i<16;++i) if(u&(0x8000u>>i)) v^=g[i]; return v;
}
// GF(64), x^6+x+1. Same field/bit orientation as pinned OP25 bch.cc. This
// fixed-storage Berlekamp-Massey/Chien decoder uses only eight syndromes and
// accepts at most four corrections, matching p25_framer.cc's conservative
// radius rather than bchDec's full eleven-bit capability. Clean NIDs take the
// exact-encode fast path; no codeword scan, heap, or mutable tables are needed.
static const uint8_t nid_exp[63]={
 1,2,4,8,16,32,3,6,12,24,48,35,5,10,20,40,
 19,38,15,30,60,59,53,41,17,34,7,14,28,56,51,37,
 9,18,36,11,22,44,27,54,47,29,58,55,45,25,50,39,
 13,26,52,43,21,42,23,46,31,62,63,61,57,49,33
};
static const uint8_t nid_log[64]={
 0,0,1,6,2,12,7,26,3,32,13,35,8,48,27,18,
 4,24,33,16,14,52,36,54,9,45,49,38,28,41,19,56,
 5,62,25,11,34,31,17,47,15,23,53,51,37,44,55,40,
 10,61,46,30,50,22,39,43,29,60,42,21,20,59,57,58
};
static uint8_t nid_mul(uint8_t a,uint8_t b) {
 if(!a || !b)return 0;
 unsigned i=nid_log[a]+nid_log[b];return nid_exp[i>=63?i-63:i];
}
static bool nid_duid_supported(unsigned duid) {
 return duid==0 || duid==3 || duid==5 || duid==7 || duid==10 || duid==15;
}
bool p25_rx_nid_decode(uint64_t nid,uint16_t *nac,uint8_t *duid,unsigned *corrected) {
 if(!nac || !duid || !corrected)return false;
 uint64_t candidate=nid;
 unsigned errors=0;
 if(nid!=nid_encode((uint16_t)(nid>>48))) {
  uint8_t s[8]={0},c[9]={1},b[9]={1},previous[9];
  // Odd syndromes; even syndromes follow from Frobenius squaring in GF(64).
  for(unsigned j=0;j<63;++j) if((nid>>(j+1))&1) {
   for(unsigned k=0;k<8;k+=2)s[k]^=nid_exp[((k+1)*j)%63];
  }
  s[1]=nid_mul(s[0],s[0]);s[3]=nid_mul(s[1],s[1]);
  s[5]=nid_mul(s[2],s[2]);s[7]=nid_mul(s[3],s[3]);
  unsigned degree=0,shift=1;uint8_t last=1;
  for(unsigned n=0;n<8;++n) {
   uint8_t discrepancy=s[n];
   for(unsigned i=1;i<=degree;++i)discrepancy^=nid_mul(c[i],s[n-i]);
   if(!discrepancy) {++shift;continue;}
   memcpy(previous,c,sizeof c);
   unsigned power=nid_log[discrepancy]+63-nid_log[last];
   uint8_t scale=nid_exp[power>=63?power-63:power];
   for(unsigned i=0;i+shift<9;++i)c[i+shift]^=nid_mul(scale,b[i]);
   if(2*degree<=n) {degree=n+1-degree;memcpy(b,previous,sizeof b);last=discrepancy;shift=1;}
   else ++shift;
  }
  if(!degree || degree>P25_NID_MAX_CORRECTED)return false;
  // An error at code coefficient j is a root at alpha^(-j).
  for(unsigned j=0;j<63;++j) {
   uint8_t x=nid_exp[j?63-j:0],value=c[degree];
   for(unsigned i=degree;i>0;--i)value=nid_mul(value,x)^c[i-1];
   if(!value) {candidate^=UINT64_C(1)<<(j+1);if(++errors>degree)return false;}
  }
  if(errors!=degree)return false;
  // Re-encoding checks every BCH bit and the original received parity bit.
  // A root count alone is insufficient to claim a valid P25 NID.
  if(candidate!=nid_encode((uint16_t)(candidate>>48)))return false;
 }
 uint16_t data=(uint16_t)(candidate>>48);
 if(!nid_duid_supported(data&15))return false;
 *nac=data>>4;*duid=(uint8_t)(data&15);*corrected=errors;return true;
}
void p25_rx_init(p25_rx *r) {memset(r,0,sizeof *r);}
void p25_rx_gap(p25_rx *r) {r->shift=0;r->symbols=0;r->expected=0;}
bool p25_rx_push(p25_rx *r,uint8_t dibit) {
 if(dibit>3) {++r->bad_input;p25_rx_gap(r);return false;}
 r->shift=((r->shift<<2)|dibit)&UINT64_C(0xffffffffffff);
 if(!r->symbols) {
  uint64_t diff=r->shift^UINT64_C(0x5575f5ff77ff);
  if(weight((uint32_t)diff)+weight((uint32_t)(diff>>32))<=2) {
   memset(r->frame,0,sizeof r->frame);
   static const uint8_t sync[6]={0x55,0x75,0xf5,0xff,0x77,0xff};
   memcpy(r->frame,sync,6);r->symbols=24;r->nid_corrected=0;++r->syncs;
  }
  return false;
 }
 unsigned pos=r->symbols++;
 r->frame[pos/4]|=(uint8_t)(dibit<<(6-2*(pos%4)));
 if(r->symbols==57) {
  uint64_t nid=0;
  for(unsigned i=24;i<57;++i) if(i!=35) nid=(nid<<2)|((r->frame[i/4]>>(6-2*(i%4)))&3);
  unsigned corrected;
  if(!p25_rx_nid_decode(nid,&r->nac,&r->duid,&corrected)) {++r->bad_nid;r->symbols=r->expected=0;return false;}
  r->nid_corrected=(uint8_t)corrected;
  if(corrected) {
   // Present the same corrected header as the parsed NAC/DUID. The interspersed
   // status dibit is not BCH-protected and remains exactly as received.
   uint64_t canonical=nid_encode((uint16_t)((r->nac<<4)|r->duid));unsigned j=0;
   for(unsigned i=24;i<57;++i) if(i!=35) {
    unsigned shift=6-2*(i%4);uint8_t mask=(uint8_t)(3u<<shift);
    r->frame[i/4]=(uint8_t)((r->frame[i/4]&~mask)|(((canonical>>(62-2*j++))&3)<<shift));
   }
  }
  switch(r->duid) {
   case 0:r->expected=396;break;
   case 3:r->expected=72;break;
   case 7:r->expected=180;break; // control channel: FSW + NID + TSBK + status symbols
   case 5:case 10:r->expected=864;break;
   case 15:r->expected=216;break;
   default:++r->bad_nid;r->symbols=0;return false;
  }
 }
 if(r->expected && r->symbols==r->expected) {
  r->symbols=r->expected=0;++r->frames;return true;
 }
 return false;
}
bool p25_rx_voice_quality(const uint8_t ldu[216],unsigned voice,uint8_t payload[11],p25_voice_quality *quality) {
 if(voice>=9 || !ldu || !payload || !quality) return false;
 uint32_t u[8],pn=0;unsigned index=0,errors=0,e0=0;
 for(unsigned j=0;j<8;++j) {
  unsigned bits=j<4?23:j<7?15:7;uint32_t v=0;
  for(unsigned b=0;b<bits;++b) v=(v<<1)|bit(ldu,p25_voice_bits[voice][index++]);
  if(j>0 && j<7) v^=pn_bits(&pn,bits);
  if(j<4) {
   uint32_t e=rx_golay_errors[(v^golay(v>>11))&2047];unsigned count=weight(e);
   errors+=count;if(!j)e0=count;u[j]=(v^e)>>11;
  } else if(j<7) {
   static const uint16_t error_table[16]={0x0,0x1,0x2,0x10,0x4,0x20,0x40,0x80,0x8,0x100,0x200,0x400,0x800,0x1000,0x2000,0x4000};
   uint32_t syndrome=(v^hamming(v>>4))&15,e=error_table[syndrome];
   errors+=weight(e);u[j]=(v^e)>>4;
  } else u[j]=v;
  if(j==0) pn=u[0]<<4;
 }
 memset(payload,0,11);unsigned out=0;
 for(unsigned j=0;j<8;++j) {
  unsigned bits=j<4?12:j<7?11:7;
  for(unsigned b=0;b<bits;++b,++out) payload[out/8]|=(uint8_t)(((u[j]>>(bits-1-b))&1)<<(7-out%8));
 }
 quality->e0=e0;quality->et=errors;return true;
}
bool p25_rx_voice(const uint8_t ldu[216],unsigned voice,uint8_t payload[11],unsigned *corrected) {
 if(!corrected)return false;
 p25_voice_quality quality;
 if(!p25_rx_voice_quality(ldu,voice,payload,&quality))return false;
 *corrected=quality.et;return true;
}
