// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIBHWSEC_FOUNDATION_FUZZED_TROUSERS_UTILS_H_
#define LIBHWSEC_FOUNDATION_FUZZED_TROUSERS_UTILS_H_

#include <stddef.h>

#include <trousers/tss.h>

#include "libhwsec-foundation/hwsec-foundation_export.h"

#if defined(__cplusplus)

#include <fuzzer/FuzzedDataProvider.h>

namespace hwsec_foundation {

HWSEC_FOUNDATION_EXPORT void FuzzedTrousersSetup(
    FuzzedDataProvider* data_provider);

}  // namespace hwsec_foundation

#endif

#if defined(__cplusplus)
extern "C" {
#endif

#define DEFINE_CONSUME_INTEGRAL(TYPE, NAME) \
  HWSEC_FOUNDATION_EXPORT TYPE FuzzedTrousersConsume##NAME();
DEFINE_CONSUME_INTEGRAL(BYTE, Byte)
DEFINE_CONSUME_INTEGRAL(TSS_BOOL, Bool)
DEFINE_CONSUME_INTEGRAL(UINT16, Uint16)
DEFINE_CONSUME_INTEGRAL(UINT32, Uint32)
DEFINE_CONSUME_INTEGRAL(UINT64, Uint64)
#undef DEFINE_CONSUME_INTEGRAL

HWSEC_FOUNDATION_EXPORT void FuzzedTrousersConsumeBytes(size_t size,
                                                        BYTE* result);

#if defined(__cplusplus)
}
#endif

#endif  // LIBHWSEC_FOUNDATION_FUZZED_TROUSERS_UTILS_H_
