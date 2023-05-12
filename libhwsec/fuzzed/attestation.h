// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIBHWSEC_FUZZED_ATTESTATION_H_
#define LIBHWSEC_FUZZED_ATTESTATION_H_

#include <memory>
#include <string>
#include <utility>

#include <attestation/proto_bindings/attestation_ca.pb.h>
#include <fuzzer/FuzzedDataProvider.h>

#include "libhwsec/backend/attestation.h"
#include "libhwsec/fuzzed/basic_objects.h"

namespace hwsec {

template <>
struct FuzzedObject<attestation::Quote> {
  attestation::Quote operator()(FuzzedDataProvider& provider) const {
    attestation::Quote quote;
    if (provider.ConsumeBool()) {
      quote.set_quote(FuzzedObject<std::string>()(provider));
    }
    if (provider.ConsumeBool()) {
      quote.set_quoted_data(FuzzedObject<std::string>()(provider));
    }
    if (provider.ConsumeBool()) {
      quote.set_quoted_pcr_value(FuzzedObject<std::string>()(provider));
    }
    if (provider.ConsumeBool()) {
      quote.set_pcr_source_hint(FuzzedObject<std::string>()(provider));
    }
    return quote;
  }
};

}  // namespace hwsec

#endif  // LIBHWSEC_FUZZED_ATTESTATION_H_
