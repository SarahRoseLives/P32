// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef P25_RX_H
#define P25_RX_H
#include <stdint.h>
#include <stdbool.h>
// Input is one hard-decision dibit (0..3). No modem assumptions or heap.
// NID BCH recovery is conservatively bounded to four bits, like pinned OP25's
// framer (the underlying BCH code can correct eleven). The received DUID parity
// bit must match the corrected codeword; it is never silently repaired.
#define P25_NID_MAX_CORRECTED 4u
typedef struct {
 uint64_t shift;
 uint8_t frame[216],duid,nid_corrected; // 0..4 corrected BCH bits, last accepted NID
 uint16_t symbols,expected,nac;
 uint32_t syncs,frames,bad_nid,bad_input;
} p25_rx;
void p25_rx_init(p25_rx *r);
void p25_rx_gap(p25_rx *r);
// True at a complete validated frame. Its NID bits are canonicalized after BCH
// correction; the received status dibit is preserved. Consume before next push.
bool p25_rx_push(p25_rx *r,uint8_t dibit);
// Decode the 64 NID bits after removing the status dibit. Supported DUIDs are
// HDU, simple TDU, LDU1, LDU2 and TDULC. Outputs change only on success.
bool p25_rx_nid_decode(uint64_t nid,uint16_t *nac,uint8_t *duid,unsigned *corrected);
typedef struct { unsigned e0,et; } p25_voice_quality;
// e0 counts corrections to the first Golay word (the whitening seed), while
// et counts all Golay/Hamming corrections. Counts are not integrity guarantees.
bool p25_rx_voice_quality(const uint8_t ldu[216],unsigned voice,uint8_t payload[11],p25_voice_quality *quality);
// Extract one voice payload from a validated LDU; reports FEC corrections.
// Golay/Hamming can miscorrect errors beyond their radius. No integrity claim.
bool p25_rx_voice(const uint8_t ldu[216],unsigned voice,uint8_t payload[11],unsigned *corrected);
#endif
