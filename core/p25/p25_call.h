// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef P25_CALL_H
#define P25_CALL_H
#include <stdbool.h>
#include <stdint.h>

typedef enum {
 P25_CALL_UNKNOWN = 0,
 P25_CALL_CLEAR = 1,
 P25_CALL_ENCRYPTED = 2,
 P25_CALL_UNSUPPORTED = 3
} p25_call_class;

// Validity describes this unit only: HDU supplies ESS/group, LDU1 supplies LC,
// LDU2 supplies ESS. A clear ESS alone does not establish a supported call type.
// All fields/validity are reset on every decode, including failure. No heap or
// mutable global storage. FEC bounds are coding guarantees, not authentication.
typedef struct {
 uint32_t source, destination;
 uint16_t group, keyid, inner_corrected;
 uint8_t mi[9];
 uint8_t algid, mfid, lco, service_options;
 uint8_t lc_options; // Raw group LC byte 3; nonzero options are unsupported.
 uint8_t protected_flag, standard_format;
 uint8_t ess_valid, lc_valid, group_valid, source_valid, destination_valid;
 uint8_t mfid_valid, rs_corrected, classification;
} p25_call_metadata;

// Packed, MSB-first complete p25_rx.frame, including status dibits; frame must
// contain at least 99 bytes for HDU, 216 for LDU1/LDU2. DUID 0/5/10 supported.
// The caller has already validated NID and full frame length. True means outer
// RS validated, including supported and unsupported/encrypted metadata.
bool p25_call_decode(const uint8_t *frame, uint8_t duid,
                     p25_call_metadata *out);
#endif
