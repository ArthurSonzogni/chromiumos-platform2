// Copyright 2022 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIBHWSEC_FUZZED_RECOVERY_CRYPTO_H_
#define LIBHWSEC_FUZZED_RECOVERY_CRYPTO_H_

#include <optional>
#include <type_traits>

#include <brillo/secure_blob.h>
#include <fuzzer/FuzzedDataProvider.h>

#include "libhwsec/backend/recovery_crypto.h"
#include "libhwsec/fuzzed/basic_objects.h"

namespace hwsec {

template <>
struct FuzzedObject<EncryptEccPrivateKeyResponse> {
  EncryptEccPrivateKeyResponse operator()(FuzzedDataProvider& provider) const {
    return EncryptEccPrivateKeyResponse{
        .encrypted_own_priv_key = FuzzedObject<brillo::Blob>()(provider),
        .extended_pcr_bound_own_priv_key =
            FuzzedObject<brillo::Blob>()(provider),
    };
  }
};

template <>
struct FuzzedObject<RecoveryCryptoRsaKeyPair> {
  RecoveryCryptoRsaKeyPair operator()(FuzzedDataProvider& provider) const {
    return RecoveryCryptoRsaKeyPair{
        .encrypted_rsa_private_key = FuzzedObject<brillo::Blob>()(provider),
        .rsa_public_key_spki_der = FuzzedObject<brillo::Blob>()(provider),
    };
  }
};

}  // namespace hwsec

#endif  // LIBHWSEC_FUZZED_RECOVERY_CRYPTO_H_
