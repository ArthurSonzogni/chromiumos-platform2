// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CRYPTOHOME_CRYPTO_HMAC_H_
#define CRYPTOHOME_CRYPTO_HMAC_H_

#include <string>

#include <brillo/secure_blob.h>

#include "cryptohome/attestation.pb.h"

namespace cryptohome {

brillo::SecureBlob HmacSha512(const brillo::SecureBlob& key,
                              const brillo::Blob& data);
brillo::SecureBlob HmacSha512(const brillo::SecureBlob& key,
                              const brillo::SecureBlob& data);

brillo::SecureBlob HmacSha256(const brillo::SecureBlob& key,
                              const brillo::Blob& data);
brillo::SecureBlob HmacSha256(const brillo::SecureBlob& key,
                              const brillo::SecureBlob& data);

// Computes an HMAC over the iv and encrypted_data fields of an EncryptedData
// protobuf.
// Parameters
//   encrypted_data - encrypted data protobuf.
//   hmac_key - secret key to use in hmac computation.
// TODO(crbug.com/1218505): add a check to guarantee that the IV field is of
// fixed length.
std::string ComputeEncryptedDataHmac(const EncryptedData& encrypted_data,
                                     const brillo::SecureBlob& hmac_key);

}  // namespace cryptohome

#endif  // CRYPTOHOME_CRYPTO_HMAC_H_
