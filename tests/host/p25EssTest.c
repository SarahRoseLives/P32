// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 The RadioFoundry Authors
// HDU/LDU2 ESS encoder round-trip: the p25_call decoder must recover MI, ALGID 0xAA, key ID, group and no RS errors.
#include <stdio.h>
#include <string.h>
#include "p25/p25_call.h"
#include "p25/tx/p25_ess.h"

static int fails;
#define CHECK(c) do { if (!(c)) { printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); fails++; } } while (0)

// The test vectors are in the tx unit, which this test does not link as a whole: include the two frames only.
#include "p25/tx/p25_test_vectors.h"

int main(void)
{
	const uint8_t mi[9] = { 0x01, 0x23, 0x45, 0x67, 0x89, 0xab, 0xcd, 0xef, 0x00 };
	uint8_t hdu[99], ldu2[216];
	p25_call_metadata m;

	CHECK(p25_ess_build_hdu(hdu, p25_test_hdu, mi, 0x1234, 1));
	CHECK(p25_call_decode(hdu, 0, &m));
	CHECK(m.ess_valid && m.algid == 0xaa && m.keyid == 1 && m.group_valid && m.group == 0x1234);
	CHECK(m.rs_corrected == 0 && memcmp(m.mi, mi, 9) == 0 && m.classification == P25_CALL_ENCRYPTED);

	CHECK(p25_ess_build_ldu2(ldu2, p25_test_ldu2, mi, 7));
	CHECK(p25_call_decode(ldu2, 10, &m));
	CHECK(m.ess_valid && m.algid == 0xaa && m.keyid == 7 && m.rs_corrected == 0 && memcmp(m.mi, mi, 9) == 0);

	if (fails) {
		printf("# FAILED p25EssTest: %d\n", fails);
		return 1;
	}
	printf("# PASSED p25EssTest\n");
	return 0;
}
