// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef P25_VOICE_POLICY_H
#define P25_VOICE_POLICY_H
#include <stdbool.h>
#include <stdint.h>

/* Receiver concealment policy, not a TIA conformance implementation.
 *
 * Comparison: pinned OP25 software_imbe_decoder.cc decode_fullrate() repeats
 * for b0 > 207, E0 >= 2 or ET >= 10 + 40 * ER; repeat_last() permits three
 * consecutive repeats and mutes the fourth. Its ER is an adaptive floating
 * error estimate and it also performs adaptive smoothing. This small policy
 * deliberately uses the conservative low-error threshold ET >= 10 without
 * claiming equivalent adaptive smoothing or standardized concealment.
 *
 * E0 is corrections in the first Golay codeword; ET includes all seven
 * protected codewords. Perfect Golay/Hamming codes may miscorrect beyond
 * their radii, and the final seven speech bits have no FEC. Passing this
 * policy is not proof that a frame is error-free or that a call is clear.
 * Metadata/call qualification must run before invoking the vocoder.
 *
 * The caller owns one last accepted 160-sample PCM frame. ACCEPT decodes the
 * supplied payload unchanged and replaces that cache; REPEAT copies the PCM
 * cache; MUTE emits zero PCM and invalidates the cache. Do not re-decode a
 * cached encoded payload: IMBE spectral prediction makes that a new decode,
 * rather than a copy of the preceding synthesis parameters. The nostar
 * invalid-pitch path also still executes sa_decode() after channel unpacking.
 *
 * Reset policy, codec and PCM cache together at a new call or call expiry.
 * On entry to MUTE, reset the codec once; the next accepted frame can then
 * recover without using arbitrarily stale spectral state. PCM repetition
 * remains bounded to 60 ms and can have a waveform discontinuity. It is a
 * conservative fallback until a separately validated parameter-repeat hook
 * is available. Healthy frames never change the codec or its arithmetic.
 */
#define P25_VOICE_MAX_REPEATS 3u
#define P25_VOICE_E0_REPEAT 2u
#define P25_VOICE_ET_REPEAT 10u

typedef enum {
    P25_VOICE_ACCEPT,
    P25_VOICE_REPEAT,
    P25_VOICE_MUTE
} p25_voice_action;

typedef struct {
    uint8_t consecutive_bad;
    bool have_good;
} p25_voice_policy;

static inline void p25_voice_policy_init(p25_voice_policy *p) {
    p->consecutive_bad=0;
    p->have_good=false;
}

/* Payload is 88 bits in the same MSB-first format as decode_4400().
 * b0 combines the top six bits of u0 and bits 2..1 of seven-bit u7.
 * No payload bytes are modified, including the unprotected final bits.
 */
static inline unsigned p25_voice_pitch(const uint8_t payload[11]) {
    return (payload[0]&0xfcu)|((payload[10]>>1)&3u);
}

static inline bool p25_voice_plausible(const uint8_t payload[11]) {
    return payload && p25_voice_pitch(payload)<=207u;
}

/* One invocation represents exactly one caller-established 20-ms voice slot.
 * The module does not infer losses from time, LDU ordering or control units.
 */
static inline p25_voice_action p25_voice_policy_missing(p25_voice_policy *p) {
    if(p->consecutive_bad<=P25_VOICE_MAX_REPEATS)++p->consecutive_bad;
    if(p->have_good && p->consecutive_bad<=P25_VOICE_MAX_REPEATS)
        return P25_VOICE_REPEAT;
    p->have_good=false;
    return P25_VOICE_MUTE;
}

static inline p25_voice_action p25_voice_policy_frame(p25_voice_policy *p,
        const uint8_t payload[11],unsigned e0,unsigned et) {
    if(!p25_voice_plausible(payload) || e0>et ||
            e0>=P25_VOICE_E0_REPEAT || et>=P25_VOICE_ET_REPEAT)
        return p25_voice_policy_missing(p);
    p->consecutive_bad=0;
    p->have_good=true;
    return P25_VOICE_ACCEPT;
}
#endif
