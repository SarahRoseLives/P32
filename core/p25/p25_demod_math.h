// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef P25_DEMOD_MATH_H
#define P25_DEMOD_MATH_H
#include <stdint.h>
// Exact Q16 truncated division, clamped before scaling to avoid excess work.
// g is positive. The unsigned remainder product fits for g <= 65535.
static inline int32_t p25_timing_error(int32_t delta,int32_t g) {
int32_t error;
if(delta>=4*g)error=4*65536;
else if(delta<=-4*g)error=-4*65536;
else if(g<=65535) {
    uint32_t magnitude=(uint32_t)(delta<0 ? -delta : delta);
    uint32_t whole=magnitude/(uint32_t)g,rem=magnitude%(uint32_t)g;
    error=(int32_t)(whole*65536+(rem*65536)/(uint32_t)g);
    if(delta<0)error=-error;
} else error=(int32_t)((int64_t)delta*65536/g);
if(error>4*65536)error=4*65536;
if(error< -4*65536)error= -4*65536;
return error;
}
// back is in [0,65535]; unsigned product fits even at int16 full scale.
static inline int32_t p25_interpolate(int16_t previous,int16_t value,uint32_t back) {
    int32_t difference=(int32_t)previous-value;
    uint32_t magnitude=(uint32_t)(difference<0 ? -difference : difference);
    int32_t correction=(int32_t)((magnitude*back)>>16);
    return value+(difference<0 ? -correction : correction);
}
#endif
