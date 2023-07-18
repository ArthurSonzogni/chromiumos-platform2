// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIBHWSEC_FUZZED_ATTESTATION_H_
#define LIBHWSEC_FUZZED_ATTESTATION_H_

#include <memory>
#include <string>
#include <utility>

#include <attestation/proto_bindings/database.pb.h>
#include <fuzzer/FuzzedDataProvider.h>

#include "libhwsec/fuzzed/protobuf.h"

namespace hwsec {

template <>
struct FuzzedObject<Attestation::CreateIdentityResult> {
  Attestation::CreateIdentityResult operator()(
      FuzzedDataProvider& provider) const {
    return Attestation::CreateIdentityResult{
        .identity_key = FuzzedObject<attestation::IdentityKey>()(provider),
        .identity_binding =
            FuzzedObject<attestation::IdentityBinding>()(provider),
    };
  }
};

}  // namespace hwsec

#endif  // LIBHWSEC_FUZZED_ATTESTATION_H_
