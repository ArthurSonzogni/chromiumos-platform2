// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "libhwsec-foundation/fuzzed_trousers_utils.h"

#include <algorithm>
#include <cstddef>
#include <cstring>
#include <memory>
#include <string>

#include <fuzzer/FuzzedDataProvider.h>
#include <trousers/tss.h>

namespace hwsec_foundation {

namespace {

struct FuzzedTrousersData {
  FuzzedDataProvider* data_provider;
};
std::unique_ptr<FuzzedTrousersData> data;

}  // namespace

void FuzzedTrousersSetup(FuzzedDataProvider* data_provider) {
  data = std::make_unique<FuzzedTrousersData>();
  data->data_provider = data_provider;
}

#define DECLARE_CONSUME_INTEGRAL(TYPE, NAME)             \
  extern "C" TYPE FuzzedTrousersConsume##NAME() {        \
    return data->data_provider->ConsumeIntegral<TYPE>(); \
  }
DECLARE_CONSUME_INTEGRAL(BYTE, Byte)
DECLARE_CONSUME_INTEGRAL(TSS_BOOL, Bool)
DECLARE_CONSUME_INTEGRAL(UINT16, Uint16)
DECLARE_CONSUME_INTEGRAL(UINT32, Uint32)
DECLARE_CONSUME_INTEGRAL(UINT64, Uint64)
#undef DECLARE_CONSUME_INTEGRAL

extern "C" void FuzzedTrousersConsumeBytes(size_t size, BYTE* result) {
  std::string bytes = data->data_provider->ConsumeBytesAsString(size);
  // Use |bytes.size()| instead of |size| because the data from
  // FuzzedDataProvider may be shorter.
  memcpy(result, bytes.data(), bytes.size());
}

}  // namespace hwsec_foundation
