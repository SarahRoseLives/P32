// SPDX-License-Identifier: GPL-3.0-or-later
// P25 control-field conventions cross-checked against pinned OP25 p25craft.py
// and p25p1_fdma.cc. GF(64): x^6+x+1, first RS root alpha^1. This bounded
// decoder has no heap/static mutable state. As with all bounded-distance FEC,
// damage beyond the designed radius can map to another valid codeword.
#include "p25_call.h"
#include <string.h>

static const uint8_t gf_exp[63] = {
 1,2,4,8,16,32,3,6,12,24,48,35,5,10,20,40,19,38,15,30,60,
 59,53,41,17,34,7,14,28,56,51,37,9,18,36,11,22,44,27,54,47,
 29,58,55,45,25,50,39,13,26,52,43,21,42,23,46,31,62,63,61,57,49,33
};
static const uint8_t gf_log[64] = {
 0,0,1,6,2,12,7,26,3,32,13,35,8,48,27,18,4,24,33,16,14,
 52,36,54,9,45,49,38,28,41,19,56,5,62,25,11,34,31,17,47,
 15,23,53,51,37,44,55,40,10,61,46,30,50,22,39,43,29,60,42,21,20,59,57,58
};
static uint8_t mul(uint8_t a,uint8_t b) {
 if(!a||!b)return 0;
 unsigned i=(unsigned)gf_log[a]+gf_log[b];if(i>=63)i-=63;return gf_exp[i];
}
static uint8_t divgf(uint8_t a,uint8_t b) {
 if(!a)return 0;
 unsigned i=(unsigned)gf_log[a]+63-gf_log[b];if(i>=63)i-=63;return gf_exp[i];
}
static uint8_t evaluate(const uint8_t *c,unsigned n,uint8_t x) {
 uint8_t v=0;for(unsigned i=0;i<n;++i)v=mul(v,x)^c[i];return v;
}

// Syndrome/Berlekamp-Massey/Chien plus a <=8x9 GF linear solve. The shortening
// prefix is immutable zero: roots in that prefix are rejected, never corrected.
static int rs_decode(uint8_t *word,unsigned n,unsigned parity) {
 uint8_t s[16],c[17]={1},b[17]={1},saved[17];
 unsigned nonzero=0,l=0,m=1;uint8_t scale=1;
 for(unsigned i=0;i<parity;++i){s[i]=evaluate(word,n,gf_exp[i+1]);nonzero|=s[i];}
 if(!nonzero)return 0;
 for(unsigned k=0;k<parity;++k){
  uint8_t d=s[k];for(unsigned i=1;i<=l;++i)d^=mul(c[i],s[k-i]);
  if(!d){++m;continue;}
  memcpy(saved,c,sizeof c);uint8_t factor=divgf(d,scale);
  for(unsigned i=0;i+m<=parity;++i)c[i+m]^=mul(factor,b[i]);
  if(2*l<=k){l=k+1-l;memcpy(b,saved,sizeof b);scale=d;m=1;}else ++m;
 }
 if(!l||l>parity/2)return -1;
 uint8_t pos[8],x[8],matrix[8][9];unsigned roots=0;
 for(unsigned i=0;i<n;++i){
  unsigned exponent=n-1-i;uint8_t inverse=gf_exp[exponent?63-exponent:0],v=0;
  for(unsigned j=l+1;j--;)v=mul(v,inverse)^c[j];
  if(!v){if(roots>=l)return -1;pos[roots]=(uint8_t)i;x[roots++]=gf_exp[exponent];}
 }
 if(roots!=l)return -1;
 for(unsigned col=0;col<l;++col){uint8_t v=x[col];for(unsigned row=0;row<l;++row){matrix[row][col]=v;v=mul(v,x[col]);}}
 for(unsigned row=0;row<l;++row)matrix[row][l]=s[row];
 for(unsigned col=0;col<l;++col){
  unsigned pivot=col;while(pivot<l&&!matrix[pivot][col])++pivot;
  if(pivot==l)return -1;
  for(unsigned j=col;j<=l;++j){uint8_t v=matrix[col][j];matrix[col][j]=matrix[pivot][j];matrix[pivot][j]=v;}
  uint8_t v=matrix[col][col];for(unsigned j=col;j<=l;++j)matrix[col][j]=divgf(matrix[col][j],v);
  for(unsigned row=0;row<l;++row)if(row!=col){v=matrix[row][col];for(unsigned j=col;j<=l;++j)matrix[row][j]^=mul(v,matrix[col][j]);}
 }
 for(unsigned i=0;i<l;++i){if(!matrix[i][l])return -1;word[pos[i]]^=matrix[i][l];}
 for(unsigned i=0;i<parity;++i)if(evaluate(word,n,gf_exp[i+1]))return -1;
 return (int)l;
}
static unsigned weight(uint32_t v){unsigned n=0;while(v){v&=v-1;++n;}return n;}
// Index with status dibits removed; status begins at bit 70, then every 72.
static unsigned data_bit(const uint8_t *frame,unsigned i){i+=2*(i/70);return(frame[i/8]>>(7-i%8))&1;}
static uint32_t codeword(const uint8_t *frame,uint8_t duid,unsigned symbol){
 unsigned bits=duid==0?18:10;
 unsigned start=duid==0?112+symbol*18:400+(symbol/4)*184+(symbol%4)*10;
 uint32_t v=0;for(unsigned j=0;j<bits;++j)v=(v<<1)|data_bit(frame,start+j);return v;
}
static uint32_t golay18(unsigned data){
 static const uint32_t g[6]={0x206cd,0x10367,0x08dc6,0x04a97,0x0293e,0x018eb};
 uint32_t v=0;for(unsigned i=0;i<6;++i)if(data&(32u>>i))v^=g[i];return v;
}
static unsigned nearest_golay(uint32_t word){
 unsigned best=19,data=0;for(unsigned i=0;i<64;++i){unsigned d=weight(word^golay18(i));if(d<best){best=d;data=i;if(!d)break;}}return data;
}
static uint32_t hamming10(unsigned data){
 static const uint16_t g[6]={0x20e,0x10d,0x08b,0x047,0x023,0x01c};
 uint32_t v=0;for(unsigned i=0;i<6;++i)if(data&(32u>>i))v^=g[i];return v;
}
static unsigned decode_hamming(uint32_t word){
 unsigned syndrome=(word^hamming10(word>>4))&15;
 // The six omitted data coordinates have no single-bit representative.
 static const uint16_t error[16]={0,1,2,0x20,4,0,0,0x40,8,0,0,0x80,0x10,0x100,0x200,0};
 return (word^error[syndrome])>>4;
}
static void unpack(const uint8_t *symbols,uint8_t *bytes,unsigned count){
 for(unsigned i=0;i<count;++i){unsigned bit=i*8,j=bit/6,shift=bit%6;uint32_t v=(uint32_t)symbols[j]<<12;
  if(j+1<(count*8+5)/6)v|=(uint32_t)symbols[j+1]<<6;
  if(j+2<(count*8+5)/6)v|=symbols[j+2];
  bytes[i]=(uint8_t)(v>>(10-shift));}
}
bool p25_call_decode(const uint8_t *frame,uint8_t duid,p25_call_metadata *out){
 if(!out)return false;
 memset(out,0,sizeof *out);
 if(!frame||(duid!=0&&duid!=5&&duid!=10))return false;
 uint8_t symbols[36],bytes[15];unsigned n=duid==0?36:24,parity=duid==0?16:duid==5?12:8;
 for(unsigned i=0;i<n;++i){uint32_t v=codeword(frame,duid,i);symbols[i]=(uint8_t)(duid==0?nearest_golay(v):decode_hamming(v));}
 int corrected=rs_decode(symbols,n,parity);if(corrected<0)return false;
 out->rs_corrected=(uint8_t)corrected;
 for(unsigned i=0;i<n;++i)out->inner_corrected+=(uint16_t)weight(codeword(frame,duid,i)^(duid==0?golay18(symbols[i]):hamming10(symbols[i])));
 unpack(symbols,bytes,(n-parity)*6/8);
 if(duid==0||duid==10){
  out->ess_valid=1;memcpy(out->mi,bytes,9);
  if(duid==0){out->mfid=bytes[9];out->mfid_valid=1;out->algid=bytes[10];out->keyid=(uint16_t)((bytes[11]<<8)|bytes[12]);out->group=(uint16_t)((bytes[13]<<8)|bytes[14]);out->group_valid=1;}
  else{out->algid=bytes[9];out->keyid=(uint16_t)((bytes[10]<<8)|bytes[11]);}
  out->classification=out->algid!=0x80?P25_CALL_ENCRYPTED:duid==0&&out->mfid!=0?P25_CALL_UNSUPPORTED:P25_CALL_CLEAR;
 }else{
  out->lc_valid=1;out->protected_flag=bytes[0]>>7;out->standard_format=(bytes[0]>>6)&1;out->lco=bytes[0]&63;
  // Protected LC contents are not interpretable without a key.
  if(out->protected_flag){out->classification=P25_CALL_ENCRYPTED;return true;}
  out->mfid=bytes[1];out->mfid_valid=1;out->service_options=bytes[2];
  if(!out->standard_format&&out->mfid==0&&(out->lco==0||out->lco==3)){
   out->source=((uint32_t)bytes[6]<<16)|((uint32_t)bytes[7]<<8)|bytes[8];out->source_valid=1;
   if(out->lco==0){out->lc_options=bytes[3];out->group=(uint16_t)((bytes[4]<<8)|bytes[5]);out->group_valid=1;}
   else{out->destination=((uint32_t)bytes[3]<<16)|((uint32_t)bytes[4]<<8)|bytes[5];out->destination_valid=1;}
  }
  out->classification=out->service_options&0x40?P25_CALL_ENCRYPTED:
   // Byte 3 includes the explicit-source-required S bit and reserved bits.
   // This receiver implements abbreviated group identity only; none of those
   // options may authorize a supported clear call merely because LCO is zero.
   out->standard_format||out->mfid!=0||out->lco!=0||out->lc_options||(out->service_options&0x30)?P25_CALL_UNSUPPORTED:P25_CALL_CLEAR;
 }
 return true;
}
