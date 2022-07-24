// Copyright 2022 The ChromiumOS Authors.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIBHWSEC_BACKEND_TPM1_STATIC_UTILS_H_
#define LIBHWSEC_BACKEND_TPM1_STATIC_UTILS_H_

#include <brillo/secure_blob.h>
#include <crypto/scoped_openssl_types.h>

#include "libhwsec/hwsec_export.h"
#include "libhwsec/overalls/overalls.h"
#include "libhwsec/status.h"

namespace hwsec {

HWSEC_EXPORT StatusOr<crypto::ScopedRSA> ParseRsaFromTpmPubkeyBlob(
    overalls::Overalls& overalls, const brillo::Blob& pubkey);

}  // namespace hwsec

#endif  // LIBHWSEC_BACKEND_TPM1_STATIC_UTILS_H_
