// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 The RadioFoundry Authors
// Compile the fixed-point encoder and its helpers together so GCC can inline the arithmetic in the inner loops
// without LTO. The decoder stays in its own translation units; the original nostar notices remain in each file.
#include "basicop2.cc"
#include "math_sub.cc"
#include "aux_sub.cc"
#include "dsp_sub.cc"
#include "dc_rmv.cc"
#include "pe_lpf.cc"
#ifdef IMBE_PITCH_SPECIALIZE
#include "imbe_pitch_specialized.cc"
#else
#include "pitch_est.cc"
#endif
#include "pitch_ref.cc"
#include "v_uv_det.cc"
#include "qnt_sub.cc"
#include "sa_encode.cc"
#include "ch_encode.cc"
#include "encode.cc"
