// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIBHWSEC_STRUCTURES_ATTESTATION_H_
#define LIBHWSEC_STRUCTURES_ATTESTATION_H_

#include <optional>

#include <brillo/secure_blob.h>

namespace hwsec {

struct Quotation {
  // Note that this is empty when using multiple DeviceConfig to quote.
  std::optional<brillo::Blob> device_config_value;
  brillo::Blob signed_data;
  brillo::Blob signature;
};

}  // namespace hwsec

#endif  // LIBHWSEC_STRUCTURES_ATTESTATION_H_
